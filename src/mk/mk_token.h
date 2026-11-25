/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .mk 词法单元定义
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_TOKEN_H_
#define _INCLUDE_MK_TOKEN_H_

#include <crystal_cluster.h>

/*
 * .mk 词法单元类型。
 * 关键字使用独立类型，Parser 不需要再做标识符文本比较；
 * TRUE / FALSE 属于布尔字面量，但按关键字表识别。
 */
typedef enum {
    MK_TOK_EOF = 0,
    MK_TOK_IDENT,
    MK_TOK_INT,
    MK_TOK_FLOAT,
    MK_TOK_STRING,

    MK_TOK_KW_STRING,
    MK_TOK_KW_INT,
    MK_TOK_KW_BOOL,
    MK_TOK_KW_FLOAT,
    MK_TOK_KW_LIST,
    MK_TOK_KW_CONST,
    MK_TOK_KW_IF,
    MK_TOK_KW_ELSE,
    MK_TOK_KW_FOR,
    MK_TOK_KW_IN,
    MK_TOK_KW_WHILE,
    MK_TOK_KW_BREAK,
    MK_TOK_KW_CONTINUE,
    MK_TOK_KW_FUNC,
    MK_TOK_KW_RETURN,
    MK_TOK_KW_FROM,
    MK_TOK_KW_AS,
    MK_TOK_KW_TRUE,
    MK_TOK_KW_FALSE,
    MK_TOK_KW_VOID,
    MK_TOK_UNDERSCORE,

    MK_TOK_AT_INCLUDE,
    MK_TOK_AT_IMPORT,
    MK_TOK_CONFIG_REF,

    MK_TOK_LPAREN,
    MK_TOK_RPAREN,
    MK_TOK_LBRACE,
    MK_TOK_RBRACE,
    MK_TOK_LBRACKET,
    MK_TOK_RBRACKET,
    MK_TOK_COMMA,
    MK_TOK_SEMI,
    MK_TOK_DOT,

    MK_TOK_PLUS,
    MK_TOK_MINUS,
    MK_TOK_STAR,
    MK_TOK_SLASH,
    MK_TOK_PERCENT,
    MK_TOK_ASSIGN,
    MK_TOK_PLUS_ASSIGN,
    MK_TOK_EQ,
    MK_TOK_NE,
    MK_TOK_LT,
    MK_TOK_GT,
    MK_TOK_LE,
    MK_TOK_GE,
    MK_TOK_AND,
    MK_TOK_OR,
    MK_TOK_NOT,
    MK_TOK_ARROW
} MKTokenType;

/*
 * 一个词法单元。
 * offset / width 始终覆盖源码中的完整词法单元：
 *   offset 为起始字节偏移，width 为包含引号、'@'、'@{' 与 '}' 的字节宽度。
 * text / len 指向源码切片，不复制、不保证以 0 结尾，范围按 token 类型约定：
 *   - 标识符、关键字、数字、运算符、标点：完整词法单元；
 *   - 字符串：去掉外层双引号后的原始字节（可能含反斜杠转义）；
 *   - @include / @import：不含 '@' 的指令名；
 *   - @{...}：花括号内部原样字节。
 * string / string_len 只用于字符串：解码后的字节，借自 Lexer 的临时缓冲，
 * 在下一次 mk_lexer_next 或 mk_lexer_clear 前有效，可包含 0 字节。
 * Parser 必须在读取到字符串 token 时立即复制到 AST 拥有的内存，不得缓存 token
 * 序列后统一构造 AST。
 * line / col 从 1 开始，col 按字节计数，指向完整词法单元的起点。
 */
typedef struct mk_token {
    MKTokenType type;
    CCUINT64 offset;
    CCUINT64 width;
    const char *text;
    CCUINT32 len;
    const char *string;
    CCUINT32 string_len;
    CCUINT32 line;
    CCUINT32 col;
} MKToken;

/* 词法单元类型的中文名称，用于诊断与测试 */
const char *mk_token_type_name(MKTokenType type);

#endif
