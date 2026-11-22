/*
 * @Description: CMK Parser 测试辅助：解析源码、读取 AST 节点与子节点。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_PARSER_TEST_UTIL_H_
#define _INCLUDE_CMK_PARSER_TEST_UTIL_H_

#include "test_harness.h"

#include "cmk/cmk_parser.h"

typedef struct cmk_test_parse {
    CMKLexer lexer;
    CMKParser parser;
    CMKAST *ast;
} CMKTestParse;

/* 解析 src；成功时 ctx->ast 有效，失败时 ctx->ast 为 NULL */
CCBOOL cmk_test_parse_begin(
    CMKTestParse *ctx, const char *src, const CMKLimits *limits
);
void cmk_test_parse_end(CMKTestParse *ctx);

const CMKASTNode *cmk_test_node(
    const CMKTestParse *ctx, CMKASTNodeId id
);
CMKASTNodeId cmk_test_child(
    const CMKTestParse *ctx, CMKASTNodeId id, CCUINT32 index
);
CCUINT32 cmk_test_child_count(
    const CMKTestParse *ctx, CMKASTNodeId id
);
CMKASTNodeId cmk_test_first_module_item(const CMKTestParse *ctx);
CMKASTNodeId cmk_test_expr_of_stmt(
    const CMKTestParse *ctx, CMKASTNodeId stmt
);

#endif
