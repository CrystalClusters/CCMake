/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .cmk 词法单元定义
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_TOKEN_H_
#define _INCLUDE_CMK_TOKEN_H_

#include <crystal_cluster.h>

/*
 * .cmk 词法单元类型。
 * 关键字使用独立类型，Parser 不需要再做标识符文本比较；
 * TRUE / FALSE 属于布尔字面量，但按关键字表识别。
 */
typedef enum {
    CMK_TOK_EOF = 0,
    CMK_TOK_IDENT,
    CMK_TOK_INT,
    CMK_TOK_FLOAT,
    CMK_TOK_STRING,

    CMK_TOK_KW_STRING,
    CMK_TOK_KW_INT,
    CMK_TOK_KW_BOOL,
    CMK_TOK_KW_FLOAT,
    CMK_TOK_KW_LIST,
    CMK_TOK_KW_ACTION,
    CMK_TOK_KW_TARGET,
    CMK_TOK_KW_TARGET_REF,
    CMK_TOK_KW_CONST,
    CMK_TOK_KW_IF,
    CMK_TOK_KW_ELSE,
    CMK_TOK_KW_FOR,
    CMK_TOK_KW_IN,
    CMK_TOK_KW_WHILE,
    CMK_TOK_KW_BREAK,
    CMK_TOK_KW_CONTINUE,
    CMK_TOK_KW_FUNC,
    CMK_TOK_KW_RETURN,
    CMK_TOK_KW_FROM,
    CMK_TOK_KW_AS,
    CMK_TOK_KW_TRUE,
    CMK_TOK_KW_FALSE,
    CMK_TOK_KW_VOID,
    CMK_TOK_UNDERSCORE,

    CMK_TOK_AT_INCLUDE,
    CMK_TOK_AT_IMPORT,
    CMK_TOK_CONFIG_REF,

    CMK_TOK_LPAREN,
    CMK_TOK_RPAREN,
    CMK_TOK_LBRACE,
    CMK_TOK_RBRACE,
    CMK_TOK_LBRACKET,
    CMK_TOK_RBRACKET,
    CMK_TOK_COMMA,
    CMK_TOK_SEMI,
    CMK_TOK_DOT,

    CMK_TOK_PLUS,
    CMK_TOK_MINUS,
    CMK_TOK_STAR,
    CMK_TOK_SLASH,
    CMK_TOK_PERCENT,
    CMK_TOK_ASSIGN,
    CMK_TOK_PLUS_ASSIGN,
    CMK_TOK_EQ,
    CMK_TOK_NE,
    CMK_TOK_LT,
    CMK_TOK_GT,
    CMK_TOK_LE,
    CMK_TOK_GE,
    CMK_TOK_AND,
    CMK_TOK_OR,
    CMK_TOK_NOT,
    CMK_TOK_ARROW
} CMKTokenType;

/*
 * 一个词法单元。
 * text / len 指向源码切片，不复制、不保证以 0 结尾：
 *   - 标识符、关键字：标识符自身；
 *   - 字符串：去掉外层双引号后的原始字节（可能含反斜杠转义）；
 *   - @include / @import：不含 '@' 的指令名；
 *   - @{...}：花括号内部原样字节。
 * string / string_len 只用于字符串：解码后的字节，借自 Lexer 的临时缓冲，
 * 在下一次 cmk_lexer_next 或 cmk_lexer_clear 前有效，可包含 0 字节。
 * line / col 从 1 开始，col 按字节计数。
 */
typedef struct cmk_token {
    CMKTokenType type;
    const char *text;
    CCUINT32 len;
    const char *string;
    CCUINT32 string_len;
    CCUINT32 line;
    CCUINT32 col;
} CMKToken;

/* 词法单元类型的中文名称，用于诊断与测试 */
const char *cmk_token_type_name(CMKTokenType type);

#endif
