/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .cmk 词法分析器
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_LEXER_H_
#define _INCLUDE_CMK_LEXER_H_

#include "cmk_diagnostic.h"
#include "cmk_source.h"
#include "cmk_token.h"

/*
 * 单遍词法分析器。
 * file / data 为借用指针，必须在 Lexer 生命周期内保持有效；
 * 字符串解码缓冲与诊断对象由 Lexer 持有，通过 cmk_lexer_clear 释放。
 */
typedef struct cmk_lexer {
    const char *file;
    const char *data;
    CCUINT64 size;
    CCUINT64 pos;
    CCUINT32 line;
    CCUINT32 col;
    char *string_buf;
    CCUINT32 string_cap;
    CMKDiagnostic diagnostic;
    CCBOOL failed;
} CMKLexer;

void cmk_lexer_init(
    CMKLexer *lex, const char *file,
    const char *data, CCUINT64 size
);

void cmk_lexer_init_source(CMKLexer *lex, const CMKSource *source);

void cmk_lexer_clear(CMKLexer *lex);

/*
 * 读取下一个词法单元。成功返回 CCTRUE；到达文件末尾时持续返回 EOF。
 * 词法错误返回 CCFALSE 并写入诊断；失败后后续调用不再推进。
 * 调用方负责保证 token 非空；token 内容在下一次调用前有效。
 */
CCBOOL cmk_lexer_next(CMKLexer *lex, CMKToken *token);

const CMKDiagnostic *cmk_lexer_diagnostic(const CMKLexer *lex);

#endif
