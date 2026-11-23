/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .ccm 文件的 @include 指令扫描
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccm_internal.h"
#include "ccm_safe.h"

#include <ctype.h>

typedef enum {
    SCAN_NORMAL = 0,
    SCAN_LINE_COMMENT,
    SCAN_BLOCK_COMMENT,
    SCAN_STRING
} SCAN_STATE;

static CCBOOL scan_fail(const char *display_path, CCUINT32 line, const char *message)
{
    color_print(
        CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
        "配置文件定位失败：%s:%u: %s\n",
        display_path ? display_path : "(.ccm)",
        line,
        message
    );
    return CCFALSE;
}

/* 行尾推进：CRLF 算一次换行，单独 CR 也按换行处理 */
static void scan_newline(
    const char *content,
    CCUINT64 size,
    CCUINT64 *index,
    CCUINT32 *line,
    CCUINT32 *column
)
{
    if (content[*index] == '\r' && *index + 1 < size && content[*index + 1] == '\n')
        *index += 2;
    else
        *index += 1;
    (*line)++;
    *column = 1;
}

/* 跳过空白与注释；用于 @include 各 token 之间 */
static CCBOOL scan_skip_trivia(
    const char *content,
    CCUINT64 size,
    CCUINT64 *index,
    CCUINT32 *line,
    CCUINT32 *column,
    const char *display_path
)
{
    while (*index < size)
    {
        char c = content[*index];
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v')
        {
            (*index)++;
            (*column)++;
            continue;
        }
        if (c == '\n' || c == '\r')
        {
            scan_newline(content, size, index, line, column);
            continue;
        }
        if (c == '/' && *index + 1 < size && content[*index + 1] == '/')
        {
            *index += 2;
            *column += 2;
            while (*index < size && content[*index] != '\n' && content[*index] != '\r')
            {
                (*index)++;
                (*column)++;
            }
            continue;
        }
        if (c == '/' && *index + 1 < size && content[*index + 1] == '*')
        {
            CCUINT32 start_line = *line;
            *index += 2;
            *column += 2;
            for (;;)
            {
                if (*index + 1 >= size)
                    return scan_fail(display_path, start_line, "块注释未闭合");
                if (content[*index] == '*' && content[*index + 1] == '/')
                {
                    *index += 2;
                    *column += 2;
                    break;
                }
                if (content[*index] == '\n' || content[*index] == '\r')
                {
                    scan_newline(content, size, index, line, column);
                    continue;
                }
                (*index)++;
                (*column)++;
            }
            continue;
        }
        break;
    }
    return CCTRUE;
}

static CCBOOL scan_push_include(
    CCDyn *out_includes,
    CCUINT32 line,
    CCUINT32 column,
    char *literal
)
{
    CCMakeCCMInclude item;
    memset(&item, 0, sizeof(item));
    item.literal = literal;
    item.line = line;
    item.column = column;
    item.child = CC_CCM_NIL;

    if (!ccm_dyn_push(
            out_includes,
            (CCUINT32)sizeof(CCMakeCCMInclude),
            (CCINT32)sizeof(CCMakeCCMInclude),
            &item
        ))
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL scan_handle_include(
    const char *content, CCUINT64 size,
    const char *display_path,
    CCUINT64 *index, CCUINT32 *line, CCUINT32 *column,
    CCUINT32 depth, CCDyn *out_includes, CCUINT32 max_edges
)
{
    CCUINT32 at_line = *line;
    CCUINT32 at_column = *column;
    CCUINT64 ident_begin = *index + 1;
    CCUINT64 ident_end = ident_begin;
    while (ident_end < size &&
           (isalnum((unsigned char)content[ident_end]) ||
            content[ident_end] == '_'))
        ident_end++;

    size_t ident_len = (size_t)(ident_end - ident_begin);
    CCBOOL is_include = (ident_len == 7 &&
        memcmp(content + ident_begin, "include", 7) == 0)
        ? CCTRUE : CCFALSE;
    if (!is_include)
    {
        *column += (CCUINT32)(ident_end - *index);
        *index = ident_end;
        return CCTRUE;
    }

    if (depth != 0)
        return scan_fail(
            display_path, at_line, "@include 只能出现在顶层"
        );
    if (max_edges != 0 &&
        out_includes->size /
        (CCUINT32)sizeof(CCMakeCCMInclude) >= max_edges)
        return scan_fail(
            display_path, at_line, "include 数量超过限制"
        );

    *column += (CCUINT32)(ident_end - *index);
    *index = ident_end;
    if (!scan_skip_trivia(
            content, size, index, line, column, display_path
        ))
        return CCFALSE;
    if (*index >= size || content[*index] != '"')
        return scan_fail(
            display_path, at_line, "@include 后缺少双引号路径"
        );

    (*index)++;
    (*column)++;
    CCUINT64 path_begin = *index;
    while (*index < size && content[*index] != '"')
    {
        unsigned char pc = (unsigned char)content[*index];
        if (pc == '\0')
            return scan_fail(
                display_path, at_line,
                "文件中包含 NUL 字节，疑似二进制文件"
            );
        if (pc < 0x20)
            return scan_fail(
                display_path, at_line,
                "@include 路径包含非法控制字符"
            );
        (*index)++;
        (*column)++;
    }
    if (*index >= size)
        return scan_fail(
            display_path, at_line, "@include 路径字符串未闭合"
        );

    CCUINT64 path_len = *index - path_begin;
    if (path_len == 0)
        return scan_fail(display_path, at_line, "@include 路径为空");

    char *literal = ccalloc(
        NULL, path_len + 1,
        CC_DBG_REL_TAG("@include 路径", NULL)
    );
    if (!literal)
        return scan_fail(display_path, at_line, "内存分配失败");
    memcpy(literal, content + path_begin, (size_t)path_len);
    literal[path_len] = '\0';
    (*index)++;
    (*column)++;

    if (!scan_skip_trivia(
            content, size, index, line, column, display_path
        ))
    {
        ccalloc(literal, 0, NULL);
        return CCFALSE;
    }
    if (*index >= size || content[*index] != ';')
    {
        ccalloc(literal, 0, NULL);
        return scan_fail(
            display_path, at_line, "@include 语句缺少分号"
        );
    }
    (*index)++;
    (*column)++;

    if (!scan_push_include(
            out_includes, at_line, at_column, literal
        ))
    {
        ccalloc(literal, 0, NULL);
        return scan_fail(display_path, at_line, "内存分配失败");
    }
    return CCTRUE;
}

CCBOOL ccm_scan_includes(
    const char *content,
    CCUINT64 size,
    const char *display_path,
    CCDyn *out_includes,
    CCUINT32 max_edges
)
{
    if (!content || !out_includes)
        return CCFALSE;

    CCUINT64 index = 0;
    CCUINT32 line = 1;
    CCUINT32 column = 1;
    CCUINT32 depth = 0;
    CCUINT32 string_line = 0;
    CCUINT32 comment_line = 0;
    SCAN_STATE state = SCAN_NORMAL;

    /* 编码标记：UTF-8 BOM 跳过；UTF-16 / UTF-32 明确拒绝 */
    if (size >= 4 &&
        (((unsigned char)content[0] == 0xFF && (unsigned char)content[1] == 0xFE &&
          content[2] == '\0' && content[3] == '\0') ||
         (content[0] == '\0' && content[1] == '\0' &&
          (unsigned char)content[2] == 0xFE && (unsigned char)content[3] == 0xFF)))
        return scan_fail(display_path, 1, "不支持 UTF-32 编码，请使用 ASCII/UTF-8");
    if (size >= 2 &&
        (((unsigned char)content[0] == 0xFF && (unsigned char)content[1] == 0xFE) ||
         ((unsigned char)content[0] == 0xFE && (unsigned char)content[1] == 0xFF)))
        return scan_fail(display_path, 1, "不支持 UTF-16 编码，请使用 ASCII/UTF-8");
    if (size >= 3 &&
        (unsigned char)content[0] == 0xEF && (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF)
        index = 3;

    while (index < size)
    {
        char c = content[index];

        if (c == '\0')
            return scan_fail(display_path, line, "文件中包含 NUL 字节，疑似二进制文件");

        if (c == '\n' || c == '\r')
        {
            scan_newline(content, size, &index, &line, &column);
            if (state == SCAN_LINE_COMMENT)
                state = SCAN_NORMAL;
            continue;
        }

        switch (state)
        {
        case SCAN_LINE_COMMENT:
            index++;
            column++;
            break;

        case SCAN_BLOCK_COMMENT:
            if (c == '*' && index + 1 < size && content[index + 1] == '/')
            {
                index += 2;
                column += 2;
                state = SCAN_NORMAL;
            }
            else
            {
                index++;
                column++;
            }
            break;

        case SCAN_STRING:
            if (c == '"')
                state = SCAN_NORMAL;
            index++;
            column++;
            break;

        case SCAN_NORMAL:
        default:
            if (c == '/' && index + 1 < size && content[index + 1] == '/')
            {
                index += 2;
                column += 2;
                state = SCAN_LINE_COMMENT;
            }
            else if (c == '/' && index + 1 < size && content[index + 1] == '*')
            {
                comment_line = line;
                index += 2;
                column += 2;
                state = SCAN_BLOCK_COMMENT;
            }
            else if (c == '"')
            {
                string_line = line;
                index++;
                column++;
                state = SCAN_STRING;
            }
            else if (c == '{' || c == '[')
            {
                depth++;
                index++;
                column++;
            }
            else if (c == '}' || c == ']')
            {
                if (depth > 0)
                    depth--;
                index++;
                column++;
            }
            else if (c == '@')
            {
                if (!scan_handle_include(
                        content, size, display_path,
                        &index, &line, &column, depth,
                        out_includes, max_edges
                    ))
                    return CCFALSE;
            }
            else
            {
                index++;
                column++;
            }
            break;
        }
    }

    if (state == SCAN_STRING)
        return scan_fail(display_path, string_line ? string_line : line, "字符串未闭合");
    if (state == SCAN_BLOCK_COMMENT)
        return scan_fail(display_path, comment_line ? comment_line : line, "块注释未闭合");
    return CCTRUE;
}
