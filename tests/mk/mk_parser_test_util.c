/*
 * @Description: MK Parser 测试辅助：解析源码、读取 AST 节点与子节点。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_parser_test_util.h"

#include <string.h>

CCBOOL mk_test_parse_begin(
    MKTestParse *ctx, const char *src, const MKLimits *limits
)
{
    if (!ctx || !src)
        return CCFALSE;

    memset(ctx, 0, sizeof(*ctx));
    if (!mk_lexer_init(
            &ctx->lexer, "test.mk", src, (CCUINT64)strlen(src)
        ))
        return CCFALSE;
    if (!mk_parser_init(&ctx->parser, &ctx->lexer, limits))
        return CCFALSE;
    if (!mk_parser_parse(&ctx->parser, &ctx->ast))
        return CCFALSE;
    return CCTRUE;
}

void mk_test_parse_end(MKTestParse *ctx)
{
    if (!ctx)
        return;
    if (ctx->ast)
    {
        mk_ast_destroy(ctx->ast);
        ctx->ast = NULL;
    }
    mk_parser_clear(&ctx->parser);
    mk_lexer_clear(&ctx->lexer);
    memset(ctx, 0, sizeof(*ctx));
}

const MKASTNode *mk_test_node(
    const MKTestParse *ctx, MKASTNodeId id
)
{
    if (!ctx || !ctx->ast)
        return NULL;
    return mk_ast_node(ctx->ast, id);
}

MKASTNodeId mk_test_child(
    const MKTestParse *ctx, MKASTNodeId id, CCUINT32 index
)
{
    const MKASTNode *node = mk_test_node(ctx, id);
    if (!node)
        return MK_AST_NODE_NIL;

    MKASTNodeId child = node->first_child;
    CCUINT32 i = 0;
    while (child != MK_AST_NODE_NIL && i < index)
    {
        const MKASTNode *child_node = mk_test_node(ctx, child);
        if (!child_node)
            return MK_AST_NODE_NIL;
        child = child_node->next_sibling;
        i++;
    }
    return child;
}

CCUINT32 mk_test_child_count(
    const MKTestParse *ctx, MKASTNodeId id
)
{
    const MKASTNode *node = mk_test_node(ctx, id);
    if (!node)
        return 0;

    CCUINT32 count = 0;
    MKASTNodeId child = node->first_child;
    while (child != MK_AST_NODE_NIL)
    {
        count++;
        const MKASTNode *child_node = mk_test_node(ctx, child);
        if (!child_node)
            break;
        child = child_node->next_sibling;
    }
    return count;
}

MKASTNodeId mk_test_first_module_item(const MKTestParse *ctx)
{
    if (!ctx || !ctx->ast)
        return MK_AST_NODE_NIL;
    return mk_test_child(ctx, mk_ast_root(ctx->ast), 0);
}

MKASTNodeId mk_test_expr_of_stmt(
    const MKTestParse *ctx, MKASTNodeId stmt
)
{
    const MKASTNode *node = mk_test_node(ctx, stmt);
    if (!node || node->kind != MK_AST_EXPR_STMT)
        return MK_AST_NODE_NIL;
    return mk_test_child(ctx, stmt, 0);
}
