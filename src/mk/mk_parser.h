/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK Parser 公共接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_PARSER_H_
#define _INCLUDE_MK_PARSER_H_

#include "mk_ast.h"
#include "mk_lexer.h"
#include "mk_limits.h"

/*
 * Parser 借用 Lexer；诊断、Token 缓冲与预算由 Parser 持有。
 * ast 只在 mk_parser_parse 执行期间借用，发布或失败后置空。
 * used 为 CCTRUE 后不允许再次 parse，除非先 clear 再 init。
 */
typedef struct mk_parser {
    MKLexer *lexer;

    MKAST *ast;
    MKToken current;
    MKToken previous;
    MKToken lookahead;
    CCBOOL has_lookahead;

    MKDiagnostic diagnostic;
    MKLimits limits;

    CCUINT32 token_count;
    CCUINT32 syntax_depth;
    CCUINT32 expression_depth;
    CCUINT32 declaration_count;

    CCBOOL failed;
    CCBOOL used;
} MKParser;

/*
 * 初始化 Parser 并读取第一个 Token。
 * 失败时 Parser 进入可预测失败状态，仍可 mk_parser_clear。
 * limits 为 NULL 时使用 mk_limits_init 的默认值。
 */
CCBOOL mk_parser_init(
    MKParser *parser, MKLexer *lexer, const MKLimits *limits
);

void mk_parser_clear(MKParser *parser);

/*
 * 解析当前 Lexer 流。进入时 *out_ast 置 NULL。
 * 只有成功并通过 mk_ast_validate 才发布 AST；失败由 Parser 销毁临时 AST。
 */
CCBOOL mk_parser_parse(MKParser *parser, MKAST **out_ast);

const MKDiagnostic *mk_parser_diagnostic(const MKParser *parser);

#endif
