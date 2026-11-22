/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 语句 Parser：块、if/while/for、return、赋值与表达式语句
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_parser_internal.h"

#include <string.h>

/* ============================================================================
 * 简单语句
 * ========================================================================== */

static CMKASTNodeId parser_parse_return(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, CMK_TOK_KW_RETURN, "`return`"))
        return CMK_AST_NODE_NIL;

    CCBOOL has_value = CCFALSE;
    CMKASTNodeId value = CMK_AST_NODE_NIL;
    if (parser->current.type != CMK_TOK_SEMI)
    {
        has_value = CCTRUE;
        value = parser_parse_expression(parser);
        if (value == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
    }
    if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    CMKASTNodeId node = parser_new_node(
        parser, CMK_AST_RETURN, &span
    );
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CMKASTNode *return_node = cmk_ast_node_mut(parser->ast, node);
    if (!return_node)
        return CMK_AST_NODE_NIL;
    return_node->as.return_stmt.has_value = has_value;
    if (has_value && !parser_append_child(parser, node, value))
        return CMK_AST_NODE_NIL;
    return node;
}

static CMKASTNodeId parser_parse_break_continue(
    CMKParser *parser, CMKTokenType type, CMKASTKind kind, const char *what
)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, type, what))
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    return parser_new_node(parser, kind, &span);
}

static CMKASTNodeId parser_parse_assignment(CMKParser *parser)
{
    CMKToken start = parser->current;
    CMKToken target_token = parser->current;

    CMKASTNodeId target = CMK_AST_NODE_NIL;
    if (target_token.type == CMK_TOK_IDENT)
    {
        CMKASTStringId name = parser_new_string(
            parser, target_token.text, target_token.len
        );
        if (name == CMK_AST_STRING_NIL)
            return CMK_AST_NODE_NIL;
        CMKSourceSpan span = parser_token_span(&target_token);
        target = parser_new_node(parser, CMK_AST_IDENT, &span);
        if (target == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
        CMKASTNode *target_node = cmk_ast_node_mut(parser->ast, target);
        if (!target_node)
            return CMK_AST_NODE_NIL;
        target_node->as.ident.name = name;
    }
    else
    {
        CMKSourceSpan span = parser_token_span(&target_token);
        target = parser_new_node(parser, CMK_AST_DISCARD, &span);
        if (target == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
    }

    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;

    CMKAssignOp op;
    if (parser->current.type == CMK_TOK_ASSIGN)
        op = CMK_ASSIGN_SET;
    else if (parser->current.type == CMK_TOK_PLUS_ASSIGN)
        op = CMK_ASSIGN_ADD;
    else
    {
        (void)parser_error_current(
            parser, "赋值语句需要 `=` 或 `+=`"
        );
        return CMK_AST_NODE_NIL;
    }
    if (op == CMK_ASSIGN_ADD && target_token.type == CMK_TOK_UNDERSCORE)
    {
        (void)parser_error_current(parser, "`_` 不能使用 `+=`");
        return CMK_AST_NODE_NIL;
    }
    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId value = parser_parse_expression(parser);
    if (value == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    CMKASTNodeId node = parser_new_node(parser, CMK_AST_ASSIGN, &span);
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *assign = cmk_ast_node_mut(parser->ast, node);
    if (!assign)
        return CMK_AST_NODE_NIL;
    assign->as.assign.op = op;
    if (!parser_append_child(parser, node, target) ||
        !parser_append_child(parser, node, value))
        return CMK_AST_NODE_NIL;
    return node;
}

static CMKASTNodeId parser_parse_expression_statement(CMKParser *parser)
{
    CMKToken start = parser->current;
    CMKASTNodeId expression = parser_parse_expression(parser);
    if (expression == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    CMKASTNodeId node = parser_new_node(
        parser, CMK_AST_EXPR_STMT, &span
    );
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    if (!parser_append_child(parser, node, expression))
        return CMK_AST_NODE_NIL;
    return node;
}

/* ============================================================================
 * 控制流
 * ========================================================================== */

CMKASTNodeId parser_parse_statement(CMKParser *parser);

static CMKASTNodeId parser_parse_if(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_enter_syntax(parser))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId condition = CMK_AST_NODE_NIL;
    CMKASTNodeId then_branch = CMK_AST_NODE_NIL;
    CMKASTNodeId else_branch = CMK_AST_NODE_NIL;
    CCBOOL has_else = CCFALSE;

    if (!parser_expect(parser, CMK_TOK_KW_IF, "`if`"))
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
        goto Failed;
    condition = parser_parse_expression(parser);
    if (condition == CMK_AST_NODE_NIL)
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
        goto Failed;
    then_branch = parser_parse_statement(parser);
    if (then_branch == CMK_AST_NODE_NIL)
        goto Failed;

    if (parser->current.type == CMK_TOK_KW_ELSE)
    {
        has_else = CCTRUE;
        if (!parser_advance(parser))
            goto Failed;
        else_branch = parser_parse_statement(parser);
        if (else_branch == CMK_AST_NODE_NIL)
            goto Failed;
    }

    {
        CMKSourceSpan span =
            parser_make_span(&start, &parser->previous);
        CMKASTNodeId node = parser_new_node(parser, CMK_AST_IF, &span);
        if (node == CMK_AST_NODE_NIL)
            goto Failed;
        CMKASTNode *if_node = cmk_ast_node_mut(parser->ast, node);
        if (!if_node)
            goto Failed;
        if_node->as.if_stmt.has_else = has_else;
        if (!parser_append_child(parser, node, condition) ||
            !parser_append_child(parser, node, then_branch) ||
            (has_else && !parser_append_child(parser, node, else_branch)))
            goto Failed;
        parser_leave_syntax(parser);
        return node;
    }

Failed:
    parser_leave_syntax(parser);
    return CMK_AST_NODE_NIL;
}

static CMKASTNodeId parser_parse_while(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_enter_syntax(parser))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId condition = CMK_AST_NODE_NIL;
    CMKASTNodeId body = CMK_AST_NODE_NIL;

    if (!parser_expect(parser, CMK_TOK_KW_WHILE, "`while`"))
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
        goto Failed;
    condition = parser_parse_expression(parser);
    if (condition == CMK_AST_NODE_NIL)
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
        goto Failed;
    body = parser_parse_statement(parser);
    if (body == CMK_AST_NODE_NIL)
        goto Failed;

    {
        CMKSourceSpan span =
            parser_make_span(&start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_WHILE, &span
        );
        if (node == CMK_AST_NODE_NIL)
            goto Failed;
        if (!parser_append_child(parser, node, condition) ||
            !parser_append_child(parser, node, body))
            goto Failed;
        parser_leave_syntax(parser);
        return node;
    }

Failed:
    parser_leave_syntax(parser);
    return CMK_AST_NODE_NIL;
}

static CMKASTNodeId parser_parse_for(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_enter_syntax(parser))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId type = CMK_AST_NODE_NIL;
    CMKASTNodeId iterable = CMK_AST_NODE_NIL;
    CMKASTNodeId body = CMK_AST_NODE_NIL;
    CMKASTStringId name = CMK_AST_STRING_NIL;

    if (!parser_expect(parser, CMK_TOK_KW_FOR, "`for`"))
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
        goto Failed;
    type = parser_parse_type(parser);
    if (type == CMK_AST_NODE_NIL)
        goto Failed;
    if (!parser_take_identifier(parser, &name))
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_KW_IN, "`in`"))
        goto Failed;
    iterable = parser_parse_expression(parser);
    if (iterable == CMK_AST_NODE_NIL)
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
        goto Failed;
    body = parser_parse_statement(parser);
    if (body == CMK_AST_NODE_NIL)
        goto Failed;

    {
        CMKSourceSpan span =
            parser_make_span(&start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_FOR_IN, &span
        );
        if (node == CMK_AST_NODE_NIL)
            goto Failed;
        CMKASTNode *for_node = cmk_ast_node_mut(parser->ast, node);
        if (!for_node)
            goto Failed;
        for_node->as.for_in.name = name;
        if (!parser_append_child(parser, node, type) ||
            !parser_append_child(parser, node, iterable) ||
            !parser_append_child(parser, node, body))
            goto Failed;
        parser_leave_syntax(parser);
        return node;
    }

Failed:
    parser_leave_syntax(parser);
    return CMK_AST_NODE_NIL;
}

/* ============================================================================
 * 块与语句分派
 * ========================================================================== */

static CMKASTNodeId parser_parse_block_item(CMKParser *parser)
{
    if (parser->current.type == CMK_TOK_KW_CONST ||
        parser_is_type_start(parser))
        return parser_parse_var_decl(parser);
    return parser_parse_statement(parser);
}

CMKASTNodeId parser_parse_block(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, CMK_TOK_LBRACE, "左花括号"))
        return CMK_AST_NODE_NIL;
    if (!parser_enter_syntax(parser))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId block = parser_new_node(parser, CMK_AST_BLOCK, NULL);
    if (block == CMK_AST_NODE_NIL)
        goto Failed;

    while (parser->current.type != CMK_TOK_RBRACE)
    {
        if (parser->current.type == CMK_TOK_EOF)
        {
            (void)parser_error_current(parser, "代码块未闭合");
            goto Failed;
        }
        CMKASTNodeId item = parser_parse_block_item(parser);
        if (item == CMK_AST_NODE_NIL)
            goto Failed;
        if (!parser_append_child(parser, block, item))
            goto Failed;
    }

    if (!parser_expect(parser, CMK_TOK_RBRACE, "右花括号"))
        goto Failed;

    {
        CMKSourceSpan span =
            parser_make_span(&start, &parser->previous);
        if (!parser_set_node_span(parser, block, &span))
            goto Failed;
    }

    parser_leave_syntax(parser);
    return block;

Failed:
    parser_leave_syntax(parser);
    return CMK_AST_NODE_NIL;
}

CMKASTNodeId parser_parse_statement(CMKParser *parser)
{
    switch (parser->current.type)
    {
    case CMK_TOK_LBRACE:
        return parser_parse_block(parser);

    case CMK_TOK_KW_IF:
        return parser_parse_if(parser);

    case CMK_TOK_KW_WHILE:
        return parser_parse_while(parser);

    case CMK_TOK_KW_FOR:
        return parser_parse_for(parser);

    case CMK_TOK_KW_RETURN:
        return parser_parse_return(parser);

    case CMK_TOK_KW_BREAK:
        return parser_parse_break_continue(
            parser, CMK_TOK_KW_BREAK, CMK_AST_BREAK, "`break`"
        );

    case CMK_TOK_KW_CONTINUE:
        return parser_parse_break_continue(
            parser, CMK_TOK_KW_CONTINUE, CMK_AST_CONTINUE, "`continue`"
        );

    case CMK_TOK_IDENT:
    case CMK_TOK_UNDERSCORE:
    {
        CMKToken next;
        memset(&next, 0, sizeof(next));
        if (!parser_peek(parser, &next))
            return CMK_AST_NODE_NIL;
        if (next.type == CMK_TOK_ASSIGN ||
            next.type == CMK_TOK_PLUS_ASSIGN)
            return parser_parse_assignment(parser);
        return parser_parse_expression_statement(parser);
    }

    default:
        return parser_parse_expression_statement(parser);
    }
}
