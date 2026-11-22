/*
 * @Description: CMK Parser 测试辅助：解析源码、读取 AST 节点与子节点。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_parser_test_util.h"

#include <string.h>

CCBOOL cmk_test_parse_begin(
    CMKTestParse *ctx, const char *src, const CMKLimits *limits
)
{
    if (!ctx || !src)
        return CCFALSE;

    memset(ctx, 0, sizeof(*ctx));
    if (!cmk_lexer_init(
            &ctx->lexer, "test.cmk", src, (CCUINT64)strlen(src)
        ))
        return CCFALSE;
    if (!cmk_parser_init(&ctx->parser, &ctx->lexer, limits))
        return CCFALSE;
    if (!cmk_parser_parse(&ctx->parser, &ctx->ast))
        return CCFALSE;
    return CCTRUE;
}

void cmk_test_parse_end(CMKTestParse *ctx)
{
    if (!ctx)
        return;
    if (ctx->ast)
    {
        cmk_ast_destroy(ctx->ast);
        ctx->ast = NULL;
    }
    cmk_parser_clear(&ctx->parser);
    cmk_lexer_clear(&ctx->lexer);
    memset(ctx, 0, sizeof(*ctx));
}

const CMKASTNode *cmk_test_node(
    const CMKTestParse *ctx, CMKASTNodeId id
)
{
    if (!ctx || !ctx->ast)
        return NULL;
    return cmk_ast_node(ctx->ast, id);
}

CMKASTNodeId cmk_test_child(
    const CMKTestParse *ctx, CMKASTNodeId id, CCUINT32 index
)
{
    const CMKASTNode *node = cmk_test_node(ctx, id);
    if (!node)
        return CMK_AST_NODE_NIL;

    CMKASTNodeId child = node->first_child;
    CCUINT32 i = 0;
    while (child != CMK_AST_NODE_NIL && i < index)
    {
        const CMKASTNode *child_node = cmk_test_node(ctx, child);
        if (!child_node)
            return CMK_AST_NODE_NIL;
        child = child_node->next_sibling;
        i++;
    }
    return child;
}

CCUINT32 cmk_test_child_count(
    const CMKTestParse *ctx, CMKASTNodeId id
)
{
    const CMKASTNode *node = cmk_test_node(ctx, id);
    if (!node)
        return 0;

    CCUINT32 count = 0;
    CMKASTNodeId child = node->first_child;
    while (child != CMK_AST_NODE_NIL)
    {
        count++;
        const CMKASTNode *child_node = cmk_test_node(ctx, child);
        if (!child_node)
            break;
        child = child_node->next_sibling;
    }
    return count;
}

CMKASTNodeId cmk_test_first_module_item(const CMKTestParse *ctx)
{
    if (!ctx || !ctx->ast)
        return CMK_AST_NODE_NIL;
    return cmk_test_child(ctx, cmk_ast_root(ctx->ast), 0);
}

CMKASTNodeId cmk_test_expr_of_stmt(
    const CMKTestParse *ctx, CMKASTNodeId stmt
)
{
    const CMKASTNode *node = cmk_test_node(ctx, stmt);
    if (!node || node->kind != CMK_AST_EXPR_STMT)
        return CMK_AST_NODE_NIL;
    return cmk_test_child(ctx, stmt, 0);
}
