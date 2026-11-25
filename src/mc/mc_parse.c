/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .mc 第二阶段：内容解析与全局变量树写入
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mc_internal.h"
#include "mc_safe.h"
#include "mc_scan_util.h"
#include "ccvar.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>

#include "ccmake_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 诊断
 * ========================================================================== */

CCBOOL mc_parse_failf(
    const char *file,
    CCUINT32 line,
    const char *fmt,
    ...
)
{
    va_list count;
    va_start(count, fmt);
    int need = vsnprintf(NULL, 0, fmt, count);
    va_end(count);
    if (need < 0)
        return CCFALSE;

    char *text = ccalloc(
        NULL, (CCUINT64)need + 1,
        CC_DBG_REL_TAG("MC 诊断", NULL)
    );
    if (!text)
        return CCFALSE;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, (size_t)need + 1, fmt, ap);
    va_end(ap);
    color_print(
        CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
        "配置文件解析失败：%s:%u: %s\n",
        file ? file : "(.mc)", line, text
    );
    ccalloc(text, 0, NULL);
    return CCFALSE;
}

/* ============================================================================
 * 词法
 * ========================================================================== */

typedef enum {
    MC_TOK_EOF = 0,
    MC_TOK_STRING,
    MC_TOK_NUMBER,
    MC_TOK_TRUE,
    MC_TOK_FALSE,
    MC_TOK_LBRACE,
    MC_TOK_RBRACE,
    MC_TOK_LBRACKET,
    MC_TOK_RBRACKET,
    MC_TOK_COLON,
    MC_TOK_SEMI,
    MC_TOK_COMMA,
    MC_TOK_AT_INCLUDE,
    MC_TOK_BUILTIN
} MCTokenType;

typedef struct cc_make_token {
    MCTokenType type;
    const char *text;
    CCUINT32 len;
    CCUINT32 line;
    CCUINT32 col;
} MCToken;

typedef struct cc_make_lexer {
    const char *data;
    CCUINT64 size;
    CCUINT64 pos;
    CCUINT32 line;
    CCUINT32 col;
    CCUINT32 max_token_bytes;
    const char *file;
} MCLexer;

static void lex_newline(MCLexer *lex)
{
    if (lex->data[lex->pos] == '\r' &&
        lex->pos + 1 < lex->size && lex->data[lex->pos + 1] == '\n')
        lex->pos += 2;
    else
        lex->pos++;
    lex->line++;
    lex->col = 1;
}

static CCBOOL lex_error(MCLexer *lex, CCUINT32 line, const char *message)
{
    return mc_parse_failf(lex->file, line, "%s", message);
}

static CCBOOL lex_token_length(MCLexer *lex, MCToken *tok, CCUINT64 n)
{
    if ((lex->max_token_bytes != 0 &&
         n > (CCUINT64)lex->max_token_bytes) ||
        n > (CCUINT64)0xFFFFFFFFu)
        return lex_error(lex, tok->line, "token 超过长度限制");
    tok->len = (CCUINT32)n;
    return CCTRUE;
}

static void lex_advance(MCLexer *lex, CCUINT64 end)
{
    while (lex->pos < end)
    {
        if (lex->data[lex->pos] == '\n' || lex->data[lex->pos] == '\r')
            lex_newline(lex);
        else
        {
            lex->pos++;
            lex->col++;
        }
    }
}

/* 跳过空白与注释 */
static CCBOOL lex_skip(MCLexer *lex)
{
    while (lex->pos < lex->size)
    {
        char c = lex->data[lex->pos];
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v')
        {
            lex->pos++;
            lex->col++;
            continue;
        }
        if (c == '\n' || c == '\r')
        {
            lex_newline(lex);
            continue;
        }
        if (c == '/' && lex->pos + 1 < lex->size && lex->data[lex->pos + 1] == '/')
        {
            lex->pos += 2;
            lex->col += 2;
            while (lex->pos < lex->size &&
                   lex->data[lex->pos] != '\n' && lex->data[lex->pos] != '\r')
            {
                lex->pos++;
                lex->col++;
            }
            continue;
        }
        if (c == '/' && lex->pos + 1 < lex->size && lex->data[lex->pos + 1] == '*')
        {
            CCUINT32 start_line = lex->line;
            lex->pos += 2;
            lex->col += 2;
            for (;;)
            {
                if (lex->pos + 1 >= lex->size)
                    return lex_error(lex, start_line, "块注释未闭合");
                if (lex->data[lex->pos] == '*' && lex->data[lex->pos + 1] == '/')
                {
                    lex->pos += 2;
                    lex->col += 2;
                    break;
                }
                if (lex->data[lex->pos] == '\n' || lex->data[lex->pos] == '\r')
                {
                    lex_newline(lex);
                    continue;
                }
                lex->pos++;
                lex->col++;
            }
            continue;
        }
        break;
    }
    return CCTRUE;
}

static CCBOOL lex_next(MCLexer *lex, MCToken *tok)
{
    if (!lex_skip(lex))
        return CCFALSE;

    memset(tok, 0, sizeof(*tok));
    tok->line = lex->line;
    tok->col = lex->col;

    if (lex->pos >= lex->size)
    {
        tok->type = MC_TOK_EOF;
        return CCTRUE;
    }

    char c = lex->data[lex->pos];
    if (c == '\0')
        return lex_error(lex, lex->line, "文件中包含 NUL 字节，疑似二进制文件");

    switch (c)
    {
    case '{': tok->type = MC_TOK_LBRACE; lex->pos++; lex->col++; return CCTRUE;
    case '}': tok->type = MC_TOK_RBRACE; lex->pos++; lex->col++; return CCTRUE;
    case '[': tok->type = MC_TOK_LBRACKET; lex->pos++; lex->col++; return CCTRUE;
    case ']': tok->type = MC_TOK_RBRACKET; lex->pos++; lex->col++; return CCTRUE;
    case ':': tok->type = MC_TOK_COLON; lex->pos++; lex->col++; return CCTRUE;
    case ';': tok->type = MC_TOK_SEMI; lex->pos++; lex->col++; return CCTRUE;
    case ',': tok->type = MC_TOK_COMMA; lex->pos++; lex->col++; return CCTRUE;
    default: break;
    }

    if (c == '"')
    {
        CCUINT64 quote = lex->pos;
        CCUINT64 end = 0;
        if (!mc_scan_string_end(lex->data, lex->size, quote, &end))
            return lex_error(lex, tok->line, "字符串未闭合");
        tok->type = MC_TOK_STRING;
        tok->text = lex->data + quote + 1;
        if (!lex_token_length(lex, tok, end - (quote + 1)))
            return CCFALSE;
        lex_advance(lex, end);
        lex->pos++;
        lex->col++;
        return CCTRUE;
    }

    if (c == '@')
    {
        lex->pos++;
        lex->col++;
        CCUINT64 begin = lex->pos;
        if (begin >= lex->size ||
            !((lex->data[begin] >= 'A' && lex->data[begin] <= 'Z') ||
              (lex->data[begin] >= 'a' && lex->data[begin] <= 'z') ||
              lex->data[begin] == '_'))
            return lex_error(lex, tok->line, "@ 后缺少指令名");
        while (lex->pos < lex->size)
        {
            char ic = lex->data[lex->pos];
            if ((ic >= 'A' && ic <= 'Z') || (ic >= 'a' && ic <= 'z') ||
                (ic >= '0' && ic <= '9') || ic == '_')
                lex->pos++;
            else
                break;
        }
        size_t n = (size_t)(lex->pos - begin);
        lex->col += (CCUINT32)(lex->pos - begin) + 1;
        if (n == 7 && memcmp(lex->data + begin, "include", 7) == 0)
        {
            tok->type = MC_TOK_AT_INCLUDE;
            tok->text = lex->data + begin;
            tok->len = (CCUINT32)n;
            return CCTRUE;
        }
        return lex_error(lex, tok->line, "未知的 @ 指令");
    }

    if (c == '$' && lex->pos + 1 < lex->size && lex->data[lex->pos + 1] == '{')
    {
        CCUINT64 open = lex->pos + 1;
        CCUINT64 end = 0;
        if (!mc_scan_builtin_end(lex->data, lex->size, open, &end))
            return lex_error(lex, tok->line, "内建函数缺少右花括号");
        CCUINT64 raw_begin = lex->pos + 2;
        tok->type = MC_TOK_BUILTIN;
        tok->text = lex->data + raw_begin;
        if (!lex_token_length(lex, tok, end - raw_begin))
            return CCFALSE;
        lex_advance(lex, end);
        lex->pos++;
        lex->col++;
        return CCTRUE;
    }

    if (c == '-' || (c >= '0' && c <= '9'))
    {
        CCUINT64 begin = lex->pos;
        if (c == '-')
        {
            lex->pos++;
            lex->col++;
            if (lex->pos >= lex->size ||
                lex->data[lex->pos] < '0' || lex->data[lex->pos] > '9')
                return lex_error(lex, tok->line, "数字格式非法");
        }
        CCBOOL has_digits = CCFALSE;
        while (lex->pos < lex->size &&
               lex->data[lex->pos] >= '0' && lex->data[lex->pos] <= '9')
        {
            has_digits = CCTRUE;
            lex->pos++;
            lex->col++;
        }
        if (!has_digits)
            return lex_error(lex, tok->line, "数字格式非法");
        if (lex->pos < lex->size && lex->data[lex->pos] == '.')
        {
            lex->pos++;
            lex->col++;
            CCBOOL frac = CCFALSE;
            while (lex->pos < lex->size &&
                   lex->data[lex->pos] >= '0' && lex->data[lex->pos] <= '9')
            {
                frac = CCTRUE;
                lex->pos++;
                lex->col++;
            }
            if (!frac)
                return lex_error(lex, tok->line, "数字格式非法");
        }
        if (lex->pos < lex->size &&
            (lex->data[lex->pos] == 'e' || lex->data[lex->pos] == 'E'))
        {
            lex->pos++;
            lex->col++;
            if (lex->pos < lex->size &&
                (lex->data[lex->pos] == '+' || lex->data[lex->pos] == '-'))
            {
                lex->pos++;
                lex->col++;
            }
            CCBOOL exp_digits = CCFALSE;
            while (lex->pos < lex->size &&
                   lex->data[lex->pos] >= '0' && lex->data[lex->pos] <= '9')
            {
                exp_digits = CCTRUE;
                lex->pos++;
                lex->col++;
            }
            if (!exp_digits)
                return lex_error(lex, tok->line, "数字格式非法");
        }
        tok->type = MC_TOK_NUMBER;
        tok->text = lex->data + begin;
        if (!lex_token_length(lex, tok, lex->pos - begin))
            return CCFALSE;
        return CCTRUE;
    }

    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
    {
        CCUINT64 begin = lex->pos;
        while (lex->pos < lex->size)
        {
            char ic = lex->data[lex->pos];
            if ((ic >= 'A' && ic <= 'Z') || (ic >= 'a' && ic <= 'z') ||
                (ic >= '0' && ic <= '9') || ic == '_')
                lex->pos++;
            else
                break;
        }
        size_t n = (size_t)(lex->pos - begin);
        lex->col += (CCUINT32)n;
        if (n == 4 && memcmp(lex->data + begin, "true", 4) == 0)
        {
            tok->type = MC_TOK_TRUE;
            return CCTRUE;
        }
        if (n == 5 && memcmp(lex->data + begin, "false", 5) == 0)
        {
            tok->type = MC_TOK_FALSE;
            return CCTRUE;
        }
        return lex_error(lex, tok->line, "无法识别的标识符");
    }

    return lex_error(lex, tok->line, "无法识别的字符");
}

/* ============================================================================
 * 语法
 * ========================================================================== */

typedef struct cc_make_parser {
    MCLexer lex;
    MCToken tok;
    CCVarTree *tree;
    CCMakeMCContext *ctx;
} MCParser;

static CCBOOL parser_advance(MCParser *p)
{
    return lex_next(&p->lex, &p->tok);
}

static CCBOOL parser_need(MCParser *p, MCTokenType type, const char *what)
{
    if (p->tok.type != type)
        return mc_parse_failf(p->lex.file, p->tok.line, "缺少%s", what);
    return CCTRUE;
}

static CCBOOL parser_expect(MCParser *p, MCTokenType type, const char *what)
{
    if (!parser_need(p, type, what))
        return CCFALSE;
    return parser_advance(p);
}

static char *parser_path_key(const char *base, const char *key, CCUINT32 key_len)
{
    size_t base_len = base ? strlen(base) : 0;
    size_t need = base_len + (base_len ? 1 : 0) + key_len + 1;
    char *out = ccalloc(
        NULL, need,
        CC_DBG_REL_TAG(".mc 字段路径", NULL)
    );
    if (!out)
        return NULL;
    size_t pos = 0;
    if (base_len)
    {
        memcpy(out, base, base_len);
        pos = base_len;
        out[pos++] = '.';
    }
    memcpy(out + pos, key, key_len);
    out[pos + key_len] = '\0';
    return out;
}

static char *parser_path_index(const char *base, CCUINT32 index)
{
    char num[16];
    int n = snprintf(num, sizeof(num), "[%u]", index);
    if (n < 0)
        return NULL;
    size_t base_len = strlen(base);
    char *out = ccalloc(
        NULL, (CCUINT64)base_len + (CCUINT64)n + 1,
        CC_DBG_REL_TAG(".mc 字段路径", NULL)
    );
    if (!out)
        return NULL;
    memcpy(out, base, base_len);
    memcpy(out + base_len, num, (size_t)n + 1);
    return out;
}

static CCBOOL parser_map_result(
    MCParser *p,
    CCVarResult result,
    const char *what
)
{
    if (result == CC_VAR_OK)
        return CCTRUE;
    if (result == CC_VAR_ERR_NOSPACE)
        return mc_parse_failf(p->lex.file, p->tok.line, "内存分配失败");
    if (result == CC_VAR_ERR_TYPE)
        return mc_parse_failf(p->lex.file, p->tok.line, "%s：类型不匹配", what);
    if (result == CC_VAR_ERR_PARAM)
        return mc_parse_failf(p->lex.file, p->tok.line, "%s：路径或参数非法", what);
    return mc_parse_failf(p->lex.file, p->tok.line, "%s：%d", what, (int)result);
}

static CCBOOL parser_parse_float(const char *text, double *out)
{
    const char *p = text;
    CCBOOL negative = CCFALSE;
    if (*p == '-')
    {
        negative = CCTRUE;
        p++;
    }

    long double value = 0.0L;
    CCBOOL has_digits = CCFALSE;
    while (*p >= '0' && *p <= '9')
    {
        value = value * 10.0L + (long double)(*p - '0');
        if (!isfinite(value))
            return CCFALSE;
        has_digits = CCTRUE;
        p++;
    }

    if (*p == '.')
    {
        p++;
        long double scale = 0.1L;
        while (*p >= '0' && *p <= '9')
        {
            value += (long double)(*p - '0') * scale;
            if (!isfinite(value))
                return CCFALSE;
            if (scale > 0.0L)
                scale *= 0.1L;
            has_digits = CCTRUE;
            p++;
        }
    }

    if (!has_digits)
        return CCFALSE;

    CCBOOL exp_negative = CCFALSE;
    CCUINT32 exponent = 0;
    CCBOOL exp_overflow = CCFALSE;
    if (*p == 'e' || *p == 'E')
    {
        p++;
        if (*p == '+' || *p == '-')
        {
            exp_negative = (*p == '-') ? CCTRUE : CCFALSE;
            p++;
        }
        if (*p < '0' || *p > '9')
            return CCFALSE;
        while (*p >= '0' && *p <= '9')
        {
            if (exponent < 1000000)
                exponent = exponent * 10 + (CCUINT32)(*p - '0');
            else
                exp_overflow = CCTRUE;
            p++;
        }
    }

    if (*p != '\0')
        return CCFALSE;

    if (value == 0.0L)
    {
        *out = negative ? -0.0 : 0.0;
        return CCTRUE;
    }
    if (exp_overflow || exponent > 10000)
        return CCFALSE;

    if (exponent > 0)
    {
        long double scale = 1.0L;
        long double base = 10.0L;
        CCUINT32 n = exponent;
        while (n > 0)
        {
            if (n & 1)
                scale *= base;
            n >>= 1;
            if (n > 0)
                base *= base;
            if (!isfinite(scale) || !isfinite(base))
                return CCFALSE;
        }
        value = exp_negative ? value / scale : value * scale;
        if (!isfinite(value))
            return CCFALSE;
    }

    if (negative)
        value = -value;
    double result = (double)value;
    if (!isfinite(result))
        return CCFALSE;
    if (result == 0.0)
        return CCFALSE;
    *out = result;
    return CCTRUE;
}

static CCBOOL parser_number(
    MCParser *p,
    CCBOOL *is_float,
    CCINT64 *integer,
    double *floating
)
{
    char *text = ccalloc(
        NULL, (CCUINT64)p->tok.len + 1,
        CC_DBG_REL_TAG(".mc 数字", NULL)
    );
    if (!text)
        return mc_parse_failf(p->lex.file, p->tok.line, "内存分配失败");
    memcpy(text, p->tok.text, p->tok.len);
    text[p->tok.len] = '\0';

    CCBOOL isf = (strchr(text, '.') || strchr(text, 'e') || strchr(text, 'E'))
                     ? CCTRUE : CCFALSE;
    errno = 0;
    CCBOOL ok = CCFALSE;
    if (isf)
    {
        double value = 0;
        if (!parser_parse_float(text, &value))
        {
            mc_parse_failf(p->lex.file, p->tok.line,
                            "浮点数格式非法或超出范围");
            goto Done;
        }
        *is_float = CCTRUE;
        *floating = value;
    }
    else
    {
        char *end = NULL;
        long long value = strtoll(text, &end, 10);
        if (errno == ERANGE || !end || *end != '\0')
        {
            mc_parse_failf(p->lex.file, p->tok.line, "整数格式非法或超出范围");
            goto Done;
        }
        *is_float = CCFALSE;
        *integer = (CCINT64)value;
    }
    ok = CCTRUE;

Done:
    ccalloc(text, 0, NULL);
    return ok;
}

static CCBOOL parser_set_value(
    MCParser *p,
    const char *path,
    CCVarType type,
    const void *data,
    CCUINT32 len,
    CCVarWriteMode mode,
    const char *what
)
{
    CCVarResult r = ccvar_set(p->tree, path, type, data, len, mode);
    return parser_map_result(p, r, what);
}

static CCBOOL parser_push_value(
    MCParser *p,
    const char *path,
    CCVarType type,
    const void *data,
    CCUINT32 len,
    CCUINT32 *out_index,
    const char *what
)
{
    CCVarResult r = ccvar_array_push(p->tree, path, type, data, len, out_index);
    return parser_map_result(p, r, what);
}


typedef enum mc_value_frame_kind {
    MC_VALUE_FRAME_OBJECT = 0,
    MC_VALUE_FRAME_ARRAY
} MCValueFrameKind;

typedef struct mc_value_frame {
    MCValueFrameKind kind;
    char *path;
    CCBOOL owns_path;
    CCUINT32 depth;
    CCUINT32 count;
    CCBOOL after_value;
} MCValueFrame;

static CCBOOL parser_value_depth_ok(MCParser *p, CCUINT32 depth)
{
    if (p->ctx->limits.max_value_depth != 0 &&
        depth > p->ctx->limits.max_value_depth)
        return mc_parse_failf(p->lex.file, p->tok.line, "值嵌套过深");
    return CCTRUE;
}

/* 返回 1=已处理标量/内建，0=不是标量，-1=错误 */
static int parser_value_scalar(
    MCParser *p, const char *path, CCBOOL append, CCUINT32 *out_index
)
{
    switch (p->tok.type)
    {
    case MC_TOK_STRING:
        if (!(append
                ? parser_push_value(
                    p, path, CC_VAR_STRING, p->tok.text, p->tok.len,
                    out_index, "追加数组字符串"
                  )
                : parser_set_value(
                    p, path, CC_VAR_STRING, p->tok.text, p->tok.len,
                    CC_VAR_WRITE_REPLACE, "写入字符串"
                  )))
            return -1;
        return parser_advance(p) ? 1 : -1;

    case MC_TOK_NUMBER:
    {
        CCBOOL is_float = CCFALSE;
        CCINT64 integer = 0;
        double floating = 0;
        if (!parser_number(p, &is_float, &integer, &floating))
            return -1;
        CCBOOL ok = append
            ? parser_push_value(
                p, path,
                is_float ? CC_VAR_FLOAT : CC_VAR_INT,
                is_float ? (const void *)&floating
                         : (const void *)&integer,
                0, out_index, "追加数组数字"
              )
            : parser_set_value(
                p, path,
                is_float ? CC_VAR_FLOAT : CC_VAR_INT,
                is_float ? (const void *)&floating
                         : (const void *)&integer,
                0, CC_VAR_WRITE_REPLACE, "写入数字"
              );
        if (!ok)
            return -1;
        return parser_advance(p) ? 1 : -1;
    }

    case MC_TOK_TRUE:
    case MC_TOK_FALSE:
    {
        CCBOOL boolean = (p->tok.type == MC_TOK_TRUE) ? CCTRUE : CCFALSE;
        CCBOOL ok = append
            ? parser_push_value(
                p, path, CC_VAR_BOOL, &boolean, 0, out_index,
                "追加数组布尔值"
              )
            : parser_set_value(
                p, path, CC_VAR_BOOL, &boolean, 0,
                CC_VAR_WRITE_REPLACE, "写入布尔值"
              );
        if (!ok)
            return -1;
        return parser_advance(p) ? 1 : -1;
    }

    case MC_TOK_BUILTIN:
    {
        char *value = NULL;
        CCUINT32 len = 0;
        if (!mc_builtin_eval(
                p->ctx, p->tok.text, p->tok.len, p->tree,
                &value, &len, p->lex.file, p->tok.line
            ))
            return -1;
        CCBOOL ok = append
            ? parser_push_value(
                p, path, CC_VAR_STRING, value, len, out_index,
                "追加内建函数结果"
              )
            : parser_set_value(
                p, path, CC_VAR_STRING, value, len,
                CC_VAR_WRITE_REPLACE, "写入内建函数结果"
              );
        if (value)
            ccalloc(value, 0, NULL);
        if (!ok)
            return -1;
        return parser_advance(p) ? 1 : -1;
    }

    default:
        return 0;
    }
}

static void parser_value_frames_clear(CCDyn *frames)
{
    MCValueFrame *items = (MCValueFrame *)frames->array;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MCValueFrame);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (items[i].owns_path && items[i].path)
            ccalloc(items[i].path, 0, NULL);
    }
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL parser_value_frame_push(
    MCParser *p, CCDyn *frames, MCValueFrameKind kind,
    char *path, CCBOOL owns_path, CCUINT32 depth
)
{
    MCValueFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = kind;
    frame.path = path;
    frame.owns_path = owns_path;
    frame.depth = depth;
    if (!mc_dyn_push(
            frames, (CCUINT32)sizeof(MCValueFrame),
            (CCINT32)sizeof(MCValueFrame), &frame
        ))
    {
        if (owns_path && path)
            ccalloc(path, 0, NULL);
        return mc_parse_failf(
            p->lex.file, p->tok.line, "内存分配失败"
        );
    }
    return CCTRUE;
}

static CCBOOL parser_value_frame_pop(
    CCDyn *frames, MCValueFrame *out
)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(MCValueFrame),
        (CCINT32)sizeof(MCValueFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static CCBOOL parser_parse_object_frame(
    MCParser *p, CCDyn *frames, MCValueFrame *frame
)
{
    if (frame->after_value)
    {
        if (!parser_need(p, MC_TOK_SEMI, "分号") ||
            !parser_advance(p))
            return CCFALSE;
        frame->after_value = CCFALSE;
        frame->count++;
        return CCTRUE;
    }

    if (p->tok.type == MC_TOK_RBRACE)
    {
        if (!parser_advance(p))
            return CCFALSE;
        MCValueFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (!parser_value_frame_pop(frames, &popped))
            return CCFALSE;
        if (popped.owns_path && popped.path)
            ccalloc(popped.path, 0, NULL);
        return CCTRUE;
    }

    if (p->tok.type == MC_TOK_EOF)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line, "对象未闭合"
        );
        return CCFALSE;
    }
    if (p->ctx->limits.max_object_members != 0 &&
        frame->count >= p->ctx->limits.max_object_members)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line, "对象成员数量超过限制"
        );
        return CCFALSE;
    }
    if (p->tok.type != MC_TOK_STRING)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line, "对象成员名必须是字符串"
        );
        return CCFALSE;
    }
    if (!ccvar_key_valid(p->tok.text, p->tok.len))
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line,
            "键名非法（应匹配 [A-Za-z_][A-Za-z0-9_]*）"
        );
        return CCFALSE;
    }

    char *child = parser_path_key(
        frame->path, p->tok.text, p->tok.len
    );
    if (!child)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line, "内存分配失败"
        );
        return CCFALSE;
    }
    if (!parser_advance(p) ||
        !parser_need(p, MC_TOK_COLON, "冒号") ||
        !parser_advance(p))
    {
        ccalloc(child, 0, NULL);
        return CCFALSE;
    }

    CCUINT32 child_depth = frame->depth + 1;
    if (!parser_value_depth_ok(p, child_depth))
    {
        ccalloc(child, 0, NULL);
        return CCFALSE;
    }

    if (p->tok.type == MC_TOK_LBRACE)
    {
        if (!parser_set_value(
                p, child, CC_VAR_OBJECT, NULL, 0,
                CC_VAR_WRITE_REPLACE, "写入对象"
            ) ||
            !parser_advance(p))
        {
            ccalloc(child, 0, NULL);
            return CCFALSE;
        }
        frame->after_value = CCTRUE;
        return parser_value_frame_push(
            p, frames, MC_VALUE_FRAME_OBJECT,
            child, CCTRUE, child_depth
        );
    }
    if (p->tok.type == MC_TOK_LBRACKET)
    {
        if (!parser_set_value(
                p, child, CC_VAR_ARRAY, NULL, 0,
                CC_VAR_WRITE_REPLACE, "写入数组"
            ) ||
            !parser_advance(p))
        {
            ccalloc(child, 0, NULL);
            return CCFALSE;
        }
        frame->after_value = CCTRUE;
        return parser_value_frame_push(
            p, frames, MC_VALUE_FRAME_ARRAY,
            child, CCTRUE, child_depth
        );
    }

    int scalar = parser_value_scalar(p, child, CCFALSE, NULL);
    ccalloc(child, 0, NULL);
    if (scalar < 0)
        return CCFALSE;
    if (scalar == 0)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line, "值格式非法"
        );
        return CCFALSE;
    }
    frame->after_value = CCTRUE;
    return CCTRUE;
}

static CCBOOL parser_parse_array_frame(
    MCParser *p, CCDyn *frames, MCValueFrame *frame
)
{
    if (p->tok.type == MC_TOK_RBRACKET)
    {
        if (!parser_advance(p))
            return CCFALSE;
        MCValueFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (!parser_value_frame_pop(frames, &popped))
            return CCFALSE;
        if (popped.owns_path && popped.path)
            ccalloc(popped.path, 0, NULL);
        return CCTRUE;
    }

    if (frame->after_value)
    {
        frame->after_value = CCFALSE;
        if (p->tok.type == MC_TOK_COMMA)
        {
            if (!parser_advance(p))
                return CCFALSE;
            return CCTRUE;
        }
    }

    if (p->tok.type == MC_TOK_EOF)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line, "数组未闭合"
        );
        return CCFALSE;
    }
    if (p->ctx->limits.max_array_elements != 0 &&
        frame->count >= p->ctx->limits.max_array_elements)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line,
            "数组元素数量超过限制"
        );
        return CCFALSE;
    }

    if (p->tok.type == MC_TOK_LBRACE ||
        p->tok.type == MC_TOK_LBRACKET)
    {
        CCBOOL is_object = (p->tok.type == MC_TOK_LBRACE)
            ? CCTRUE : CCFALSE;
        CCUINT32 index = 0;
        if (!parser_push_value(
                p, frame->path,
                is_object ? CC_VAR_OBJECT : CC_VAR_ARRAY,
                NULL, 0, &index,
                is_object ? "追加数组对象" : "追加数组"
            ) ||
            !parser_advance(p))
            return CCFALSE;
        char *child = parser_path_index(frame->path, index);
        if (!child)
        {
            (void)mc_parse_failf(
                p->lex.file, p->tok.line, "内存分配失败"
            );
            return CCFALSE;
        }
        CCUINT32 child_depth = frame->depth + 1;
        if (!parser_value_depth_ok(p, child_depth))
        {
            ccalloc(child, 0, NULL);
            return CCFALSE;
        }
        frame->count++;
        frame->after_value = CCTRUE;
        return parser_value_frame_push(
            p, frames,
            is_object ? MC_VALUE_FRAME_OBJECT : MC_VALUE_FRAME_ARRAY,
            child, CCTRUE, child_depth
        );
    }

    if (!parser_value_depth_ok(p, frame->depth + 1))
        return CCFALSE;
    int scalar = parser_value_scalar(p, frame->path, CCTRUE, NULL);
    if (scalar < 0)
        return CCFALSE;
    if (scalar == 0)
    {
        (void)mc_parse_failf(
            p->lex.file, p->tok.line, "数组元素格式非法"
        );
        return CCFALSE;
    }
    frame->count++;
    frame->after_value = CCTRUE;
    return CCTRUE;
}

static CCBOOL parser_parse_value(
    MCParser *p,
    const char *path,
    CCVarWriteMode object_mode,
    CCUINT32 depth
)
{
    if (!parser_value_depth_ok(p, depth))
        return CCFALSE;

    CCDyn frames;
    memset(&frames, 0, sizeof(frames));

    int scalar = parser_value_scalar(p, path, CCFALSE, NULL);
    if (scalar < 0)
        return CCFALSE;
    if (scalar > 0)
        return CCTRUE;

    if (p->tok.type == MC_TOK_LBRACE)
    {
        if (!parser_set_value(
                p, path, CC_VAR_OBJECT, NULL, 0,
                object_mode, "写入对象"
            ) ||
            !parser_advance(p))
            return CCFALSE;
        if (!parser_value_frame_push(
                p, &frames, MC_VALUE_FRAME_OBJECT,
                (char *)path, CCFALSE, depth
            ))
            return CCFALSE;
    }
    else if (p->tok.type == MC_TOK_LBRACKET)
    {
        if (!parser_set_value(
                p, path, CC_VAR_ARRAY, NULL, 0,
                CC_VAR_WRITE_REPLACE, "写入数组"
            ) ||
            !parser_advance(p))
            return CCFALSE;
        if (!parser_value_frame_push(
                p, &frames, MC_VALUE_FRAME_ARRAY,
                (char *)path, CCFALSE, depth
            ))
            return CCFALSE;
    }
    else
        return mc_parse_failf(p->lex.file, p->tok.line, "值格式非法");

    while (frames.size >= (CCUINT32)sizeof(MCValueFrame))
    {
        MCValueFrame *frame =
            &((MCValueFrame *)frames.array)
                [frames.size / (CCUINT32)sizeof(MCValueFrame) - 1];
        if (frame->kind == MC_VALUE_FRAME_OBJECT)
        {
            if (!parser_parse_object_frame(p, &frames, frame))
                goto Failed;
            continue;
        }
        if (!parser_parse_array_frame(p, &frames, frame))
            goto Failed;
    }

    if (frames.array)
        ccalloc(frames.array, 0, NULL);
    return CCTRUE;

Failed:
    parser_value_frames_clear(&frames);
    return CCFALSE;
}

static CCBOOL parser_parse_top_field(MCParser *p)
{
    if (p->tok.type != MC_TOK_STRING)
        return mc_parse_failf(p->lex.file, p->tok.line, "字段名必须是字符串");
    if (!ccvar_key_valid(p->tok.text, p->tok.len))
        return mc_parse_failf(
            p->lex.file, p->tok.line,
            "键名非法（应匹配 [A-Za-z_][A-Za-z0-9_]*）"
        );

    char *path = parser_path_key("", p->tok.text, p->tok.len);
    if (!path)
        return mc_parse_failf(p->lex.file, p->tok.line, "内存分配失败");
    if (!parser_advance(p) || !parser_need(p, MC_TOK_COLON, "冒号"))
        goto Failed;
    if (!parser_advance(p))
        goto Failed;
    if (!parser_parse_value(p, path, CC_VAR_WRITE_MERGE, 0))
        goto Failed;
    ccalloc(path, 0, NULL);
    return parser_expect(p, MC_TOK_SEMI, "字段结束分号");

Failed:
    ccalloc(path, 0, NULL);
    return CCFALSE;
}

static CCBOOL parser_parse_file(
    CCMakeMCContext *ctx,
    const CCMakeMCFile *file,
    CCVarTree *tree
)
{
    MCParser p;
    memset(&p, 0, sizeof(p));
    p.lex.data = file->content;
    p.lex.size = file->size;
    p.lex.line = 1;
    p.lex.col = 1;
    p.lex.max_token_bytes = ctx->limits.max_token_bytes;
    p.lex.file = file->real_path;
    p.tree = tree;
    p.ctx = ctx;

    if (!parser_advance(&p))
        return CCFALSE;

    while (p.tok.type != MC_TOK_EOF)
    {
        if (p.tok.type == MC_TOK_AT_INCLUDE)
        {
            if (!parser_advance(&p) ||
                !parser_need(&p, MC_TOK_STRING, "@include 路径"))
                return CCFALSE;
            if (!parser_advance(&p) ||
                !parser_expect(&p, MC_TOK_SEMI, "@include 分号"))
                return CCFALSE;
            continue;
        }

        if (!parser_parse_top_field(&p))
            return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL mc_parse_into(
    CCMakeMCContext *ctx, CCVarTree *tree, CCBOOL apply_defines
)
{
    if (!ctx || !tree)
        return mc_parse_failf("(.mc)", 0, "解析上下文或变量树未初始化");

    CCUINT32 count = ctx->order.size / (CCUINT32)sizeof(CCUINT32);
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCUINT32 index = 0;
        if (cc_dyn_core_seek(
                &ctx->order, (CCUINT32)sizeof(CCUINT32),
                (CCINT32)sizeof(CCUINT32), &index, i
            ) != 0
        )
            return mc_parse_failf("(.mc)", 0, "解析顺序损坏");
        const CCMakeMCFile *file = &((const CCMakeMCFile *)ctx->files.array)[index];
        if (!parser_parse_file(ctx, file, tree))
            return CCFALSE;
    }

    /* -D 覆盖：全部文件之后应用，优先级最高；键按路径段解释 */
    if (!apply_defines)
        return CCTRUE;
    CCUINT32 def_count =
        ccmake_global_var.defines.size / (CCUINT32)sizeof(CCMakeDefine);
    for (CCUINT32 i = 0; i < def_count; i++)
    {
        CCMakeDefine def;
        memset(&def, 0, sizeof(def));
        if (cc_dyn_core_seek(
                &ccmake_global_var.defines,
                (CCUINT32)sizeof(CCMakeDefine),
                (CCINT32)sizeof(CCMakeDefine),
                &def, i
            ) != 0
        )
            return mc_parse_failf("(.mc)", 0, "-D 参数读取失败");
        CCVarResult r = ccvar_set(
            tree, def.key, CC_VAR_STRING,
            def.value, (CCUINT32)strlen(def.value),
            CC_VAR_WRITE_REPLACE
        );
        if (r == CC_VAR_ERR_PARAM)
            return mc_parse_failf(
                "(.mc)", 0,
                "-D 键名非法：%s（应匹配路径段 [A-Za-z_][A-Za-z0-9_]*）",
                def.key
            );
        if (r != CC_VAR_OK)
            return mc_parse_failf("(.mc)", 0, "-D 参数应用失败：%s", def.key);
    }
    return CCTRUE;
}

CCBOOL mc_parse(CCMakeMCContext *ctx)
{
    if (!ctx)
        return CCFALSE;
    CCVarTree *tree = ccvar_global();
    if (!tree)
        return mc_parse_failf("(.mc)", 0, "全局变量树未初始化");
    return mc_parse_into(ctx, tree, CCTRUE);
}
