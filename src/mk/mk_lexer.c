/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .mk 词法分析器
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_lexer.h"

#include <ccalloc.h>

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define MK_LEXER_COMMENT CC_DBG_REL_TAG(".mk 词法分析器", NULL)

/* ============================================================================
 * 词法单元名称
 * ========================================================================== */

const char *mk_token_type_name(MKTokenType type)
{
    switch (type)
    {
    case MK_TOK_EOF:
        return "文件结束";

    case MK_TOK_IDENT:
        return "标识符";

    case MK_TOK_INT:
        return "整数";

    case MK_TOK_FLOAT:
        return "浮点数";

    case MK_TOK_STRING:
        return "字符串";

    case MK_TOK_KW_STRING:
        return "`string`";

    case MK_TOK_KW_INT:
        return "`int`";

    case MK_TOK_KW_BOOL:
        return "`bool`";

    case MK_TOK_KW_FLOAT:
        return "`float`";

    case MK_TOK_KW_LIST:
        return "`list`";

    case MK_TOK_KW_CONST:
        return "`const`";

    case MK_TOK_KW_IF:
        return "`if`";

    case MK_TOK_KW_ELSE:
        return "`else`";

    case MK_TOK_KW_FOR:
        return "`for`";

    case MK_TOK_KW_IN:
        return "`in`";

    case MK_TOK_KW_WHILE:
        return "`while`";

    case MK_TOK_KW_BREAK:
        return "`break`";

    case MK_TOK_KW_CONTINUE:
        return "`continue`";

    case MK_TOK_KW_FUNC:
        return "`func`";

    case MK_TOK_KW_RETURN:
        return "`return`";

    case MK_TOK_KW_FROM:
        return "`from`";

    case MK_TOK_KW_AS:
        return "`as`";

    case MK_TOK_KW_TRUE:
        return "`true`";

    case MK_TOK_KW_FALSE:
        return "`false`";

    case MK_TOK_KW_VOID:
        return "`void`";

    case MK_TOK_UNDERSCORE:
        return "`_`";

    case MK_TOK_AT_INCLUDE:
        return "`@include`";

    case MK_TOK_AT_IMPORT:
        return "`@import`";

    case MK_TOK_CONFIG_REF:
        return "配置引用";

    case MK_TOK_LPAREN:
        return "`(`";

    case MK_TOK_RPAREN:
        return "`)`";

    case MK_TOK_LBRACE:
        return "`{`";

    case MK_TOK_RBRACE:
        return "`}`";

    case MK_TOK_LBRACKET:
        return "`[`";

    case MK_TOK_RBRACKET:
        return "`]`";

    case MK_TOK_COMMA:
        return "`,`";

    case MK_TOK_SEMI:
        return "`;`";

    case MK_TOK_DOT:
        return "`.`";

    case MK_TOK_PLUS:
        return "`+`";

    case MK_TOK_MINUS:
        return "`-`";

    case MK_TOK_STAR:
        return "`*`";

    case MK_TOK_SLASH:
        return "`/`";

    case MK_TOK_PERCENT:
        return "`%`";

    case MK_TOK_ASSIGN:
        return "`=`";

    case MK_TOK_PLUS_ASSIGN:
        return "`+=`";

    case MK_TOK_EQ:
        return "`==`";

    case MK_TOK_NE:
        return "`!=`";

    case MK_TOK_LT:
        return "`<`";

    case MK_TOK_GT:
        return "`>`";

    case MK_TOK_LE:
        return "`<=`";

    case MK_TOK_GE:
        return "`>=`";

    case MK_TOK_AND:
        return "`&&`";

    case MK_TOK_OR:
        return "`||`";

    case MK_TOK_NOT:
        return "`!`";

    case MK_TOK_ARROW:
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

typedef struct mk_keyword_entry {
    const char *name;
    CCUINT32 len;
    MKTokenType type;
} MKKeywordEntry;

#define MK_KEYWORD(name, type) { name, (CCUINT32)(sizeof(name) - 1), type }

static const MKKeywordEntry g_mk_keywords[] = {
    MK_KEYWORD("string", MK_TOK_KW_STRING),
    MK_KEYWORD("int", MK_TOK_KW_INT),
    MK_KEYWORD("bool", MK_TOK_KW_BOOL),
    MK_KEYWORD("float", MK_TOK_KW_FLOAT),
    MK_KEYWORD("list", MK_TOK_KW_LIST),
    MK_KEYWORD("const", MK_TOK_KW_CONST),
    MK_KEYWORD("if", MK_TOK_KW_IF),
    MK_KEYWORD("else", MK_TOK_KW_ELSE),
    MK_KEYWORD("for", MK_TOK_KW_FOR),
    MK_KEYWORD("in", MK_TOK_KW_IN),
    MK_KEYWORD("while", MK_TOK_KW_WHILE),
    MK_KEYWORD("break", MK_TOK_KW_BREAK),
    MK_KEYWORD("continue", MK_TOK_KW_CONTINUE),
    MK_KEYWORD("func", MK_TOK_KW_FUNC),
    MK_KEYWORD("return", MK_TOK_KW_RETURN),
    MK_KEYWORD("from", MK_TOK_KW_FROM),
    MK_KEYWORD("as", MK_TOK_KW_AS),
    MK_KEYWORD("true", MK_TOK_KW_TRUE),
    MK_KEYWORD("false", MK_TOK_KW_FALSE),
    MK_KEYWORD("void", MK_TOK_KW_VOID),
    MK_KEYWORD("_", MK_TOK_UNDERSCORE)
};

static MKTokenType lex_keyword_lookup(const char *text, CCUINT32 len)
{
    CCUINT32 count =
        (CCUINT32)(sizeof(g_mk_keywords) / sizeof(g_mk_keywords[0]));
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (g_mk_keywords[i].len == len &&
            memcmp(g_mk_keywords[i].name, text, len) == 0)
            return g_mk_keywords[i].type;
    }
    return MK_TOK_IDENT;
}

/* ============================================================================
 * 位置与诊断
 * ========================================================================== */

static CCBOOL lex_errorf(
    MKLexer *lex,
    CCUINT32 line,
    CCUINT32 col,
    const char *fmt,
    ...
)
{
    va_list ap;
    va_start(ap, fmt);
    (void)mk_diagnostic_vset(
        &lex->diagnostic, MK_DIAG_ERROR,
        lex->file, line, col, fmt, ap
    );
    va_end(ap);
    lex->failed = CCTRUE;
    return CCFALSE;
}

static CCBOOL lex_advance_column(MKLexer *lex)
{
    if (lex->col == 0xFFFFFFFFu)
        return lex_errorf(
            lex, lex->line, lex->col,
            "列号超出表示上限"
        );
    lex->col++;
    return CCTRUE;
}

static CCBOOL lex_advance_columns(MKLexer *lex, CCUINT32 count)
{
    if (count > 0xFFFFFFFFu - lex->col)
        return lex_errorf(
            lex, lex->line, lex->col,
            "列号超出表示上限"
        );
    lex->col += count;
    return CCTRUE;
}

static CCBOOL lex_advance_line(MKLexer *lex)
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

static CCBOOL lex_newline(MKLexer *lex)
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
static CCBOOL lex_skip_trivia(MKLexer *lex)
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

static CCBOOL lex_scan_ident(MKLexer *lex, MKToken *token)
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

static CCBOOL lex_scan_number(MKLexer *lex, MKToken *token)
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

    token->type = is_float ? MK_TOK_FLOAT : MK_TOK_INT;
    token->text = lex->data + begin;
    token->len = (CCUINT32)len;
    return CCTRUE;
}

/* 保证字符串解码缓冲至少容纳 need 字节正文和结尾 0 */
static CCBOOL lex_string_scratch(MKLexer *lex, CCUINT32 need)
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
        lex->string_buf, (CCUINT64)cap, MK_LEXER_COMMENT
    );
    if (!grown)
        return lex_errorf(lex, lex->line, lex->col, "内存分配失败");
    lex->string_buf = grown;
    lex->string_cap = cap;
    return CCTRUE;
}

static CCBOOL lex_scan_string(MKLexer *lex, MKToken *token)
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

    token->type = MK_TOK_STRING;
    token->text = lex->data + begin;
    token->len = (CCUINT32)raw_len;
    token->string = lex->string_buf;
    token->string_len = decoded;

    lex->pos++;
    if (!lex_advance_column(lex))
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL lex_scan_config_ref(MKLexer *lex, MKToken *token)
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

    token->type = MK_TOK_CONFIG_REF;
    token->text = lex->data + begin;
    token->len = (CCUINT32)len;

    lex->pos++;
    if (!lex_advance_column(lex))
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL lex_scan_at(MKLexer *lex, MKToken *token)
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
        token->type = MK_TOK_AT_INCLUDE;
    else if (len == 6 && memcmp(lex->data + begin, "import", 6) == 0)
        token->type = MK_TOK_AT_IMPORT;
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

static CCBOOL lex_scan_symbol(MKLexer *lex, MKToken *token)
{
    const char *data = lex->data;
    CCUINT64 pos = lex->pos;
    CCUINT64 remain = lex->size - pos;
    MKTokenType type;
    CCUINT32 width = 1;

    switch (data[pos])
    {
    case '(':
        type = MK_TOK_LPAREN;
        break;

    case ')':
        type = MK_TOK_RPAREN;
        break;

    case '{':
        type = MK_TOK_LBRACE;
        break;

    case '}':
        type = MK_TOK_RBRACE;
        break;

    case '[':
        type = MK_TOK_LBRACKET;
        break;

    case ']':
        type = MK_TOK_RBRACKET;
        break;

    case ',':
        type = MK_TOK_COMMA;
        break;

    case ';':
        type = MK_TOK_SEMI;
        break;

    case '.':
        type = MK_TOK_DOT;
        break;

    case '+':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = MK_TOK_PLUS_ASSIGN;
            width = 2;
        }
        else
            type = MK_TOK_PLUS;
        break;

    case '-':
        if (remain >= 2 && data[pos + 1] == '>')
        {
            type = MK_TOK_ARROW;
            width = 2;
        }
        else
            type = MK_TOK_MINUS;
        break;

    case '*':
        type = MK_TOK_STAR;
        break;

    case '/':
        type = MK_TOK_SLASH;
        break;

    case '%':
        type = MK_TOK_PERCENT;
        break;

    case '=':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = MK_TOK_EQ;
            width = 2;
        }
        else
            type = MK_TOK_ASSIGN;
        break;

    case '<':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = MK_TOK_LE;
            width = 2;
        }
        else
            type = MK_TOK_LT;
        break;

    case '>':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = MK_TOK_GE;
            width = 2;
        }
        else
            type = MK_TOK_GT;
        break;

    case '!':
        if (remain >= 2 && data[pos + 1] == '=')
        {
            type = MK_TOK_NE;
            width = 2;
        }
        else
            type = MK_TOK_NOT;
        break;

    case '&':
        if (remain >= 2 && data[pos + 1] == '&')
        {
            type = MK_TOK_AND;
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
            type = MK_TOK_OR;
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
    MKLexer *lex,
    const char *file,
    const char *message
)
{
    lex->failed = CCTRUE;
    (void)mk_diagnostic_set(
        &lex->diagnostic, MK_DIAG_ERROR,
        file, 0, 0, "%s", message
    );
    return CCFALSE;
}

CCBOOL mk_lexer_init(
    MKLexer *lex, const char *file,
    const char *data, CCUINT64 size
)
{
    if (!lex)
        return CCFALSE;
    memset(lex, 0, sizeof(*lex));
    lex->line = 1;
    lex->col = 1;
    mk_diagnostic_init(&lex->diagnostic);

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

CCBOOL mk_lexer_init_source(MKLexer *lex, const MKSource *source)
{
    if (!lex)
        return CCFALSE;
    if (!source)
    {
        (void)mk_lexer_init(lex, NULL, NULL, 0);
        return lex_init_invalid(lex, NULL, "源码对象为空");
    }
    return mk_lexer_init(lex, source->path, source->data, source->size);
}

void mk_lexer_clear(MKLexer *lex)
{
    if (!lex)
        return;
    if (lex->string_buf)
        ccalloc(lex->string_buf, 0, NULL);
    mk_diagnostic_clear(&lex->diagnostic);
    memset(lex, 0, sizeof(*lex));
}

CCBOOL mk_lexer_next(MKLexer *lex, MKToken *token)
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
        token->type = MK_TOK_EOF;
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

const MKDiagnostic *mk_lexer_diagnostic(const MKLexer *lex)
{
    if (!lex)
        return NULL;
    return &lex->diagnostic;
}
