/*
 * @Description: MK Parser 测试辅助：解析源码、读取 AST 节点与子节点。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_PARSER_TEST_UTIL_H_
#define _INCLUDE_MK_PARSER_TEST_UTIL_H_

#include "test_harness.h"

#include "mk/mk_parser.h"

typedef struct mk_test_parse {
    MKLexer lexer;
    MKParser parser;
    MKAST *ast;
} MKTestParse;

/* 解析 src；成功时 ctx->ast 有效，失败时 ctx->ast 为 NULL */
CCBOOL mk_test_parse_begin(
    MKTestParse *ctx, const char *src, const MKLimits *limits
);
void mk_test_parse_end(MKTestParse *ctx);

const MKASTNode *mk_test_node(
    const MKTestParse *ctx, MKASTNodeId id
);
MKASTNodeId mk_test_child(
    const MKTestParse *ctx, MKASTNodeId id, CCUINT32 index
);
CCUINT32 mk_test_child_count(
    const MKTestParse *ctx, MKASTNodeId id
);
MKASTNodeId mk_test_first_module_item(const MKTestParse *ctx);
MKASTNodeId mk_test_expr_of_stmt(
    const MKTestParse *ctx, MKASTNodeId stmt
);

#endif
