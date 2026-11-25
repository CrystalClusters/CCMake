/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .mk 词法分析器
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_LEXER_H_
#define _INCLUDE_MK_LEXER_H_

#include "mk_diagnostic.h"
#include "mk_source.h"
#include "mk_token.h"

/*
 * 单遍词法分析器。
 *
 * 输入契约：
 *   - data == NULL 只允许与 size == 0 搭配；
 *   - size > 0 时 data 必须指向至少 size 字节的有效内存；
 *   - size 必须能用 size_t 表示；
 *   - file 借用，可为 NULL 表示匿名源码；诊断输出时显示为 "(.mk)"；
 *   - data 借用，Lexer 不复制、不接管，必须在 Lexer 生命周期内保持有效。
 *
 * 初始化失败时返回 CCFALSE，Lexer 进入可预测的失败状态：
 * 仍可调用 mk_lexer_clear 释放，mk_lexer_next 只会返回 CCFALSE。
 * 常规入口使用 mk_lexer_init_source；裸指针入口主要用于测试与特殊场景。
 */
typedef struct mk_lexer {
    const char *file;
    const char *data;
    CCUINT64 size;
    CCUINT64 pos;
    CCUINT32 line;
    CCUINT32 col;
    char *string_buf;
    CCUINT32 string_cap;
    MKDiagnostic diagnostic;
    CCBOOL failed;
} MKLexer;

CCBOOL mk_lexer_init(
    MKLexer *lex, const char *file,
    const char *data, CCUINT64 size
);

CCBOOL mk_lexer_init_source(MKLexer *lex, const MKSource *source);

void mk_lexer_clear(MKLexer *lex);

/*
 * 读取下一个词法单元。成功返回 CCTRUE；到达文件末尾时持续返回 EOF。
 * 词法错误返回 CCFALSE 并写入诊断；失败后后续调用不再推进。
 * 调用方负责保证 token 非空；token 内容在下一次调用前有效。
 */
CCBOOL mk_lexer_next(MKLexer *lex, MKToken *token);

const MKDiagnostic *mk_lexer_diagnostic(const MKLexer *lex);

#endif
