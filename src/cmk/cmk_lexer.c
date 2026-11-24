/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .cmk 词法分析器
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_lexer.h"

#include <ccalloc.h>

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define CMK_LEXER_COMMENT CC_DBG_REL_TAG(".cmk 词法分析器", NULL)

/* ============================================================================
 * 词法单元名称
 * ========================================================================== */

const char *cmk_token_type_name(CMKTokenType type)
{
    switch (type)
    {
    case CMK_TOK_EOF:
        return "文件结束";

    case CMK_TOK_IDENT:
        return "标识符";

    case CMK_TOK_INT:
        return "整数";

    case CMK_TOK_FLOAT:
        return "浮点数";

    case CMK_TOK_STRING:
        return "字符串";

    case CMK_TOK_KW_STRING:
        return "`string`";

    case CMK_TOK_KW_INT:
        return "`int`";

    case CMK_TOK_KW_BOOL:
        return "`bool`";

    case CMK_TOK_KW_FLOAT:
        return "`float`";

    case CMK_TOK_KW_LIST:
        return "`list`";

    case CMK_TOK_KW_ACTION:
        return "`action`";

    case CMK_TOK_KW_CONST:
        return "`const`";

    case CMK_TOK_KW_IF:
        return "`if`";

    case CMK_TOK_KW_ELSE:
        return "`else`";

    case CMK_TOK_KW_FOR:
        return "`for`";

    case CMK_TOK_KW_IN:
        return "`in`";

    case CMK_TOK_KW_WHILE:
        return "`while`";

    case CMK_TOK_KW_BREAK:
        return "`break`";

    case CMK_TOK_KW_CONTINUE:
        return "`continue`";

    case CMK_TOK_KW_FUNC:
        return "`func`";

    case CMK_TOK_KW_RETURN:
        return "`return`";

    case CMK_TOK_KW_FROM:
        return "`from`";

    case CMK_TOK_KW_AS:
        return "`as`";

    case CMK_TOK_KW_TRUE:
        return "`true`";

    case CMK_TOK_KW_FALSE:
        return "`false`";

    case CMK_TOK_KW_VOID:
        return "`void`";

    case CMK_TOK_UNDERSCORE:
        return "`_`";

    case CMK_TOK_AT_INCLUDE:
        return "`@include`";

    case CMK_TOK_AT_IMPORT:
        return "`@import`";

    case CMK_TOK_CONFIG_REF:
        return "配置引用";

    case CMK_TOK_LPAREN:
        return "`(`";

    case CMK_TOK_RPAREN:
        return "`)`";

    case CMK_TOK_LBRACE:
        return "`{`";

    case CMK_TOK_RBRACE:
        return "`}`";

    case CMK_TOK_LBRACKET:
        return "`[`";

    case CMK_TOK_RBRACKET:
        return "`]`";

    case CMK_TOK_COMMA:
        return "`,`";

    case CMK_TOK_SEMI:
        return "`;`";

    case CMK_TOK_DOT:
        return "`.`";

    case CMK_TOK_PLUS:
        return "`+`";

    case CMK_TOK_MINUS:
        return "`-`";

    case CMK_TOK_STAR:
        return "`*`";

    case CMK_TOK_SLASH:
        return "`/`";

    case CMK_TOK_PERCENT:
        return "`%`";

    case CMK_TOK_ASSIGN:
        return "`=`";

    case CMK_TOK_PLUS_ASSIGN:
        return "`+=`";

    case CMK_TOK_EQ:
        return "`==`";

    case CMK_TOK_NE:
        return "`!=`";

    case CMK_TOK_LT:
        return "`<`";

    case CMK_TOK_GT:
        return "`>`";

    case CMK_TOK_LE:
        return "`<=`";

    case CMK_TOK_GE:
        return "`>=`";

    case CMK_TOK_AND:
        return "`&&`";

    case CMK_TOK_OR:
        return "`||`";

    case CMK_TOK_NOT:
        return "`!`";

    case CMK_TOK_ARROW:
        return "`->`";

    default:
        return "未知词法单元";
    }
}

/* ============================================================================
 * 字符分类与关键字
 * ========================================================================== */

static CCBOOL lex_is_digit(char c)
{
    return (c >= '0' && c <= '9') ? CCTRUE : CCFALSE;
}

static CCBOOL lex_is_ident_start(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
        return CCTRUE;
    return CCFALSE;
}

static CCBOOL lex_is_ident_continue(char c)
{
    if (lex_is_ident_start(c) || lex_is_digit(c))
        return CCTRUE;
    return CCFALSE;
}

typedef struct cmk_keyword_entry {
    const char *name;
    CCUINT32 len;
    CMKTokenType type;
} CMKKeywordEntry;

#define CMK_KEYWORD(name, type) { name, (CCUINT32)(sizeof(name) - 1), type }

static const CMKKeywordEntry g_cmk_keywords[] = {
    CMK_KEYWORD("string", CMK_TOK_KW_STRING),
    CMK_KEYWORD("int", CMK_TOK_KW_INT),
    CMK_KEYWORD("bool", CMK_TOK_KW_BOOL),
    CMK_KEYWORD("float", CMK_TOK_KW_FLOAT),
    CMK_KEYWORD("list", CMK_TOK_KW_LIST),
    CMK_KEYWORD("action", CMK_TOK_KW_ACTION),
    CMK_KEYWORD("const", CMK_TOK_KW_CONST),
    CMK_KEYWORD("if", CMK_TOK_KW_IF),
    CMK_KEYWORD("else", CMK_TOK_KW_ELSE),
    CMK_KEYWORD("for", CMK_TOK_KW_FOR),
    CMK_KEYWORD("in", CMK_TOK_KW_IN),
    CMK_KEYWORD("while", CMK_TOK_KW_WHILE),
    CMK_KEYWORD("break", CMK_TOK_KW_BREAK),
    CMK_KEYWORD("continue", CMK_TOK_KW_CONTINUE),
    CMK_KEYWORD("func", CMK_TOK_KW_FUNC),
    CMK_KEYWORD("return", CMK_TOK_KW_RETURN),
    CMK_KEYWORD("from", CMK_TOK_KW_FROM),
    CMK_KEYWORD("as", CMK_TOK_KW_AS),
    CMK_KEYWORD("true", CMK_TOK_KW_TRUE),
    CMK_KEYWORD("false", CMK_TOK_KW_FALSE),
    CMK_KEYWORD("void", CMK_TOK_KW_VOID),
    CMK_KEYWORD("_", CMK_TOK_UNDERSCORE)
};

static CMKTokenType lex_keyword_lookup(const char *text, CCUINT32 len)
{
    CCUINT32 count =
        (CCUINT32)(sizeof(g_cmk_keywords) / sizeof(g_cmk_keywords[0]));
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (g_cmk_keywords[i].len == len &&
            memcmp(g_cmk_keywords[i].name, text, len) == 0)
            return g_cmk_keywords[i].type;
    }
    return CMK_TOK_IDENT;
}

/* ============================================================================
 * 位置与诊断
 * ========================================================================== */

static CCBOOL lex_errorf(
    CMKLexer *lex,
    CCUINT32 line,
    CCUINT32 col,
    const char *fmt,
    ...
)
{
    va_list ap;
    va_start(ap, fmt);
    (void)cmk_diagnostic_vset(
        &lex->diagnostic, CMK_DIAG_ERROR,
        lex->file, line, col, fmt, ap
    );
    va_end(ap);
    lex->failed = CCTRUE;
    return CCFALSE;
}

static CCBOOL lex_advance_column(CMKLexer *lex)
{
    if (lex->col == 0xFFFFFFFFu)
        return lex_errorf(
            lex, lex->line, lex->col,
            "列号超出表示上限"
        );
    lex->col++;
    return CCTRUE;
}

static CCBOOL lex_advance_columns(CMKLexer *lex, CCUINT32 count)
{
    if (count > 0xFFFFFFFFu - lex->col)
        return lex_errorf(
            lex, lex->line, lex->col,
            "列号超出表示上限"
        );
    lex->col += count;
    return CCTRUE;
}

static CCBOOL lex_advance_line(CMKLexer *lex)
{
    if (lex->line == 0xFFFFFFFFu)
        return lex_errorf(
            lex, lex->line, lex->col,
            "行号超出表示上限"
        );
    lex->line++;
    lex->col = 1;
    return CCTRUE;
}

static CCBOOL lex_newline(CMKLexer *lex)
{
    if (lex->data[lex->pos] == '\r' &&
        lex->pos + 1 < lex->size &&
        lex->data[lex->pos + 1] == '\n')
        lex->pos += 2;
    else
        lex->pos++;
    return lex_advance_line(lex);
}

/* 跳过空白与注释；块注释不嵌套 */
static CCBOOL lex_skip_trivia(CMKLexer *lex)
{
    while (lex->pos < lex->size)
    {
        char c = lex->data[lex->pos];
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v')
        {
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
            continue;
        }
        if (c == '\n' || c == '\r')
        {
            if (!lex_newline(lex))
                return CCFALSE;
            continue;
        }
        if (c == '\0')
            return lex_errorf(
                lex, lex->line, lex->col,
                "文件中包含 NUL 字节，疑似二进制文件"
            );

        if (c == '/' && lex->pos + 1 < lex->size &&
            lex->data[lex->pos + 1] == '/')
        {
            lex->pos += 2;
            if (!lex_advance_columns(lex, 2))
                return CCFALSE;
            while (lex->pos < lex->size &&
                   lex->data[lex->pos] != '\n' &&
                   lex->data[lex->pos] != '\r')
            {
                if (lex->data[lex->pos] == '\0')
                    return lex_errorf(
                        lex, lex->line, lex->col,
                        "文件中包含 NUL 字节，疑似二进制文件"
                    );
                lex->pos++;
                if (!lex_advance_column(lex))
                    return CCFALSE;
            }
            continue;
        }

        if (c == '/' && lex->pos + 1 < lex->size &&
            lex->data[lex->pos + 1] == '*')
        {
            CCUINT32 start_line = lex->line;
            CCUINT32 start_col = lex->col;
            lex->pos += 2;
            if (!lex_advance_columns(lex, 2))
                return CCFALSE;
            for (;;)
            {
                if (lex->pos >= lex->size)
                    return lex_errorf(
                        lex, start_line, start_col,
                        "块注释未闭合"
                    );
                if (lex->data[lex->pos] == '*' &&
                    lex->pos + 1 < lex->size &&
                    lex->data[lex->pos + 1] == '/')
                {
                    lex->pos += 2;
                    if (!lex_advance_columns(lex, 2))
                        return CCFALSE;
                    break;
                }
                if (lex->data[lex->pos] == '\0')
                    return lex_errorf(
                        lex, lex->line, lex->col,
                        "文件中包含 NUL 字节，疑似二进制文件"
                    );
                if (lex->data[lex->pos] == '\n' ||
                    lex->data[lex->pos] == '\r')
                {
                    if (!lex_newline(lex))
                        return CCFALSE;
                }
                else
                {
                    lex->pos++;
                    if (!lex_advance_column(lex))
                        return CCFALSE;
                }
            }
            continue;
        }
        break;
    }
    return CCTRUE;
}

/* ============================================================================
 * 词法单元扫描
 * ========================================================================== */

static CCBOOL lex_scan_ident(CMKLexer *lex, CMKToken *token)
{
    CCUINT64 begin = lex->pos;
    while (lex->pos < lex->size &&
           lex_is_ident_continue(lex->data[lex->pos]))
    {
        lex->pos++;
        if (!lex_advance_column(lex))
            return CCFALSE;
    }

    CCUINT64 len = lex->pos - begin;
    if (len >= (CCUINT64)0xFFFFFFFFu)
        return lex_errorf(
            lex, token->line, token->col,
            "标识符长度超过表示上限"
        );

    token->type = lex_keyword_lookup(
        lex->data + begin, (CCUINT32)len
    );
    token->text = lex->data + begin;
    token->len = (CCUINT32)len;
    return CCTRUE;
}

static CCBOOL lex_scan_number(CMKLexer *lex, CMKToken *token)
{
    CCUINT64 begin = lex->pos;
    CCBOOL is_float = CCFALSE;

    if (lex->data[lex->pos] == '.')
    {
        // .5 形式：小数点后必须有数字
        is_float = CCTRUE;
        lex->pos++;
        if (!lex_advance_column(lex))
            return CCFALSE;
        if (lex->pos >= lex->size ||
            !lex_is_digit(lex->data[lex->pos]))
            return lex_errorf(
                lex, lex->line, lex->col,
                "数字格式非法：小数点后缺少数字"
            );
        while (lex->pos < lex->size &&
               lex_is_digit(lex->data[lex->pos]))
        {
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
        }
    }
    else
    {
        while (lex->pos < lex->size &&
               lex_is_digit(lex->data[lex->pos]))
        {
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
        }
        if (lex->pos < lex->size && lex->data[lex->pos] == '.')
        {
            // 1. 与 1.5 都作为浮点数
            is_float = CCTRUE;
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
            while (lex->pos < lex->size &&
                   lex_is_digit(lex->data[lex->pos]))
            {
                lex->pos++;
                if (!lex_advance_column(lex))
                    return CCFALSE;
            }
        }
    }

    if (lex->pos < lex->size &&
        (lex->data[lex->pos] == 'e' || lex->data[lex->pos] == 'E'))
    {
        is_float = CCTRUE;
        lex->pos++;
        if (!lex_advance_column(lex))
            return CCFALSE;
        if (lex->pos < lex->size &&
            (lex->data[lex->pos] == '+' ||
             lex->data[lex->pos] == '-'))
        {
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
        }
        if (lex->pos >= lex->size ||
            !lex_is_digit(lex->data[lex->pos]))
            return lex_errorf(
                lex, lex->line, lex->col,
                "数字格式非法：指数部分缺少数字"
            );
        while (lex->pos < lex->size &&
               lex_is_digit(lex->data[lex->pos]))
        {
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
        }
    }

    if (lex->pos < lex->size)
    {
        char tail = lex->data[lex->pos];
        if (lex_is_ident_continue(tail) || tail == '.')
            return lex_errorf(
                lex, lex->line, lex->col,
                "数字格式非法：意外的字符"
            );
    }

    CCUINT64 len = lex->pos - begin;
    if (len >= (CCUINT64)0xFFFFFFFFu)
        return lex_errorf(
            lex, token->line, token->col,
            "数字长度超过表示上限"
        );

    token->type = is_float ? CMK_TOK_FLOAT : CMK_TOK_INT;
    token->text = lex->data + begin;
    token->len = (CCUINT32)len;
    return CCTRUE;
}

/* 保证字符串解码缓冲至少容纳 need 字节正文和结尾 0 */
static CCBOOL lex_string_scratch(CMKLexer *lex, CCUINT32 need)
{
    if ((CCUINT64)need + 1 <= (CCUINT64)lex->string_cap)
        return CCTRUE;
    if (need >= 0xFFFFFFFFu)
        return lex_errorf(
            lex, lex->line, lex->col,
            "字符串长度超过表示上限"
        );

    CCUINT32 cap = need + 1;
    char *grown = ccalloc(
        lex->string_buf, (CCUINT64)cap, CMK_LEXER_COMMENT
    );
    if (!grown)
        return lex_errorf(lex, lex->line, lex->col, "内存分配失败");
    lex->string_buf = grown;
    lex->string_cap = cap;
    return CCTRUE;
}

static CCBOOL lex_scan_string(CMKLexer *lex, CMKToken *token)
{
    CCUINT32 quote_line = lex->line;
    CCUINT32 quote_col = lex->col;

    lex->pos++;
    if (!lex_advance_column(lex))
        return CCFALSE;
    CCUINT64 begin = lex->pos;

    for (;;)
    {
        if (lex->pos >= lex->size)
            return lex_errorf(
                lex, quote_line, quote_col,
                "字符串未闭合"
            );

        char c = lex->data[lex->pos];
        if (c == '\0')
            return lex_errorf(
                lex, lex->line, lex->col,
                "文件中包含 NUL 字节，疑似二进制文件"
            );
        if (c == '"')
            break;
        if (c == '\n' || c == '\r')
            return lex_errorf(
                lex, lex->line, lex->col,
                "字符串不能跨行（需要换行请使用 \\n）"
            );

        if (c == '\\')
        {
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
            if (lex->pos >= lex->size)
                return lex_errorf(
                    lex, quote_line, quote_col,
                    "字符串未闭合"
                );

            char escape = lex->data[lex->pos];
            switch (escape)
            {
            case '\\':
            case '"':
            case '\'':
            case 'n':
            case 'r':
            case 't':
            case 'a':
            case 'b':
            case 'f':
            case 'v':
            case '0':
                break;

            default:
                return lex_errorf(
                    lex, lex->line, lex->col,
                    "字符串转义序列非法：`\\%c`", escape
                );
            }
            lex->pos++;
            if (!lex_advance_column(lex))
                return CCFALSE;
            continue;
        }

        lex->pos++;
        if (!lex_advance_column(lex))
            return CCFALSE;
    }

    CCUINT64 raw_len = lex->pos - begin;
    if (raw_len >= (CCUINT64)0xFFFFFFFFu)
        return lex_errorf(
            lex, quote_line, quote_col,
            "字符串长度超过表示上限"
        );
    if (!lex_string_scratch(lex, (CCUINT32)raw_len))
        return CCFALSE;

    CCUINT32 decoded = 0;
    for (CCUINT64 i = begin; i < lex->pos; i++)
    {
        char c = lex->data[i];
        if (c != '\\')
        {
            lex->string_buf[decoded++] = c;
            continue;
        }

        i++;
        switch (lex->data[i])
        {
        case '\\':
            lex->string_buf[decoded++] = '\\';
            break;

        case '"':
            lex->string_buf[decoded++] = '"';
            break;

        case '\'':
            lex->string_buf[decoded++] = '\'';
            break;

        case 'n':
            lex->string_buf[decoded++] = '\n';
            break;

        case 'r':
            lex->string_buf[decoded++] = '\r';
            break;

        case 't':
            lex->string_buf[decoded++] = '\t';
            break;

        case 'a':
            lex->string_buf[decoded++] = '\a';
            break;

        case 'b':
            lex->string_buf[decoded++] = '\b';
            break;

        case 'f':
            lex->string_buf[decoded++] = '\f';
            break;

        case 'v':
            lex->string_buf[decoded++] = '\v';
            break;

        case '0':
            lex->string_buf[decoded++] = '\0';
            break;
        }
    }
    lex->string_buf[decoded] = '\0';

    token->type = CMK_TOK_STRING;
    token->text = lex->data + begin;
    token->len = (CCUINT32)raw_len;
    token->string = lex->string_buf;
    token->string_len = decoded;

    lex->pos++;
    if (!lex_advance_column(lex))
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL lex_scan_config_ref(CMKLexer *lex, CMKToken *token)
{
    CCUINT32 at_line = lex->line;
    CCUINT32 at_col = lex->col;

    lex->pos += 2;
    if (!lex_advance_columns(lex, 2))
        return CCFALSE;
    CCUINT64 begin = lex->pos;

    while (lex->pos < lex->size)
    {
        char c = lex->data[lex->pos];
        if (c == '}')
            break;
        if (c == '\0')
            return lex_errorf(
                lex, lex->line, lex->col,
                "文件中包含 NUL 字节，疑似二进制文件"
            );
        if (c == '\n' || c == '\r')
            return lex_errorf(
                lex, at_line, at_col,
                "配置引用不能跨行"
            );
        if (!lex_is_ident_continue(c) &&
            c != '.' && c != '[' && c != ']')
            return lex_errorf(
                lex, lex->line, lex->col,
                "配置引用路径包含非法字符：0x%02X",
                (unsigned int)(unsigned char)c
            );
        lex->pos++;
        if (!lex_advance_column(lex))
            return CCFALSE;
    }

    if (lex->pos >= lex->size)
        return lex_errorf(lex, at_line, at_col, "配置引用未闭合");

    CCUINT64 len = lex->pos - begin;
    if (len == 0)
        return lex_errorf(lex, at_line, at_col, "配置引用缺少路径");
    if (len >= (CCUINT64)0xFFFFFFFFu)
        return lex_errorf(
            lex, at_line, at_col,
            "配置引用长度超过表示上限"
        );

    token->type = CMK_TOK_CONFIG_REF;
    token->text = lex->data + begin;
    token->len = (CCUINT32)len;

    lex->pos++;
    if (!lex_advance_column(lex))
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL lex_scan_at(CMKLexer *lex, CMKToken *token)
{
    if (lex->pos + 1 < lex->size && lex->data[lex->pos + 1] == '{')
        return lex_scan_config_ref(lex, token);

    CCUINT32 at_line = lex->line;
    CCUINT32 at_col = lex->col;

    lex->pos++;
    if (!lex_advance_column(lex))
        return CCFALSE;
    if (lex->pos < lex->size && lex->data[lex->pos] == '\0')
        return lex_errorf(
            lex, lex->line, lex->col,
            "文件中包含 NUL 字节，疑似二进制文件"
        );
    if (lex->pos >= lex->size ||
        !lex_is_ident_start(lex->data[lex->pos]))
        return lex_errorf(lex, at_line, at_col, "@ 后缺少指令名");

    CCUINT64 begin = lex->pos;
    while (lex->pos < lex->size &&
           lex_is_ident_continue(lex->data[lex->pos]))
    {
        lex->pos++;
        if (!lex_advance_column(lex))
            return CCFALSE;
    }

    CCUINT64 len = lex->pos - begin;
    if (len == 7 && memcmp(lex->data + begin, "include", 7) == 0)
        token->type = CMK_TOK_AT_INCLUDE;
    else if (len == 6 && memcmp(lex->data + begin, "import", 6) == 0)
        token->type = CMK_TOK_AT_IMPORT;
    else
    {
        CCUINT64 show = (len > 64) ? 64 : len;
        return lex_errorf(
            lex, at_line, at_col,
            "未知的 @ 指令：`@%.*s`", (int)show, lex->data + begin
        );
    }

    token->text = lex->data + begin;
    token->len = (CCUINT32)len;
    return CCTRUE;
}

static CCBOOL lex_scan_symbol(CMKLexer *lex, CMKToken *token)
{
    const char *data = lex->data;
    CCUINT64 pos = lex->pos;
    CCUINT64 remain = lex->size - pos;
    CMKTokenType type;
    CCUINT32 width = 1;

    switch (data[pos])
    {
    case '(':
        type = CMK_TOK_LPAREN;
        break;

    case ')':
        type = CMK_TOK_RPAREN;
        break;

    case '{':
        type = CMK_TOK_LBRACE;
        break;

    case '}':
        type = CMK_TOK_RBRACE;
        break;

    case '[':
        type = CMK_TOK_LBRACKET;
        break;

    case ']':
        type = CMK_TOK_RBRACKET;
        break;

    case ',':
        type = CMK_TOK_COMMA;
        break;

    case ';':
        type = CMK_TOK_SEMI;
        break;

    case '.':
        type = CMK_TOK_DOT;
        break;

    case '+':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = CMK_TOK_PLUS_ASSIGN;
            width = 2;
        }
        else
            type = CMK_TOK_PLUS;
        break;

    case '-':
        if (remain >= 2 && data[pos + 1] == '>')
        {
            type = CMK_TOK_ARROW;
            width = 2;
        }
        else
            type = CMK_TOK_MINUS;
        break;

    case '*':
        type = CMK_TOK_STAR;
        break;

    case '/':
        type = CMK_TOK_SLASH;
        break;

    case '%':
        type = CMK_TOK_PERCENT;
        break;

    case '=':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = CMK_TOK_EQ;
            width = 2;
        }
        else
            type = CMK_TOK_ASSIGN;
        break;

    case '<':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = CMK_TOK_LE;
            width = 2;
        }
        else
            type = CMK_TOK_LT;
        break;

    case '>':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = CMK_TOK_GE;
            width = 2;
        }
        else
            type = CMK_TOK_GT;
        break;

    case '!':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = CMK_TOK_NE;
            width = 2;
        }
        else
            type = CMK_TOK_NOT;
        break;

    case '&':
        if (remain >= 2 && data[pos + 1] == '&')
        {
            type = CMK_TOK_AND;
            width = 2;
        }
        else
            return lex_errorf(
                lex, lex->line, lex->col,
                "无法识别的字符：`&`（逻辑与写作 `&&`）"
            );
        break;

    case '|':
        if (remain >= 2 && data[pos + 1] == '|')
        {
            type = CMK_TOK_OR;
            width = 2;
        }
        else
            return lex_errorf(
                lex, lex->line, lex->col,
                "无法识别的字符：`|`（逻辑或写作 `||`）"
            );
        break;

    default:
        return lex_errorf(
            lex, lex->line, lex->col,
            "无法识别的字符：0x%02X",
            (unsigned int)(unsigned char)data[pos]
        );
    }

    token->type = type;
    token->text = data + pos;
    token->len = width;
    lex->pos += width;
    if (!lex_advance_columns(lex, width))
        return CCFALSE;
    return CCTRUE;
}

/* ============================================================================
 * 对外接口
 * ========================================================================== */

static CCBOOL lex_init_invalid(
    CMKLexer *lex,
    const char *file,
    const char *message
)
{
    lex->failed = CCTRUE;
    (void)cmk_diagnostic_set(
        &lex->diagnostic, CMK_DIAG_ERROR,
        file, 0, 0, "%s", message
    );
    return CCFALSE;
}

CCBOOL cmk_lexer_init(
    CMKLexer *lex, const char *file,
    const char *data, CCUINT64 size
)
{
    if (!lex)
        return CCFALSE;
    memset(lex, 0, sizeof(*lex));
    lex->line = 1;
    lex->col = 1;
    cmk_diagnostic_init(&lex->diagnostic);

    if ((!data && size > 0) || size > (CCUINT64)SIZE_MAX)
        return lex_init_invalid(
            lex, file,
            "词法分析器初始化参数非法：源码指针或长度无效"
        );

    lex->file = file;
    lex->data = data;
    lex->size = size;
    return CCTRUE;
}

CCBOOL cmk_lexer_init_source(CMKLexer *lex, const CMKSource *source)
{
    if (!lex)
        return CCFALSE;
    if (!source)
    {
        (void)cmk_lexer_init(lex, NULL, NULL, 0);
        return lex_init_invalid(lex, NULL, "源码对象为空");
    }
    return cmk_lexer_init(lex, source->path, source->data, source->size);
}

void cmk_lexer_clear(CMKLexer *lex)
{
    if (!lex)
        return;
    if (lex->string_buf)
        ccalloc(lex->string_buf, 0, NULL);
    cmk_diagnostic_clear(&lex->diagnostic);
    memset(lex, 0, sizeof(*lex));
}

CCBOOL cmk_lexer_next(CMKLexer *lex, CMKToken *token)
{
    if (!lex || !token)
        return CCFALSE;

    memset(token, 0, sizeof(*token));
    if (lex->failed)
        return CCFALSE;
    if (!lex->data && lex->size > 0)
        return lex_errorf(
            lex, lex->line, lex->col,
            "源码缓冲区无效"
        );
    if (!lex_skip_trivia(lex))
        return CCFALSE;

    token->offset = lex->pos;
    token->line = lex->line;
    token->col = lex->col;

    if (lex->pos >= lex->size)
    {
        token->type = CMK_TOK_EOF;
        return CCTRUE;
    }

    char c = lex->data[lex->pos];
    if (c == '\0')
        return lex_errorf(
            lex, lex->line, lex->col,
            "文件中包含 NUL 字节，疑似二进制文件"
        );

    CCBOOL ok;
    if (lex_is_ident_start(c))
        ok = lex_scan_ident(lex, token);
    else if (lex_is_digit(c))
        ok = lex_scan_number(lex, token);
    else if (c == '.' && lex->pos + 1 < lex->size &&
             lex_is_digit(lex->data[lex->pos + 1]))
        ok = lex_scan_number(lex, token);
    else if (c == '"')
        ok = lex_scan_string(lex, token);
    else if (c == '@')
        ok = lex_scan_at(lex, token);
    else
        ok = lex_scan_symbol(lex, token);

    if (!ok)
        return CCFALSE;
    token->width = lex->pos - token->offset;
    return CCTRUE;
}

const CMKDiagnostic *cmk_lexer_diagnostic(const CMKLexer *lex)
{
    if (!lex)
        return NULL;
    return &lex->diagnostic;
}
