/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 语句 Parser：显式语句帧栈、块、if/while/for、return、赋值与表达式语句
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_parser_internal.h"
#include "cmk_safe.h"

#include <ccalloc.h>

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
    CMKToken assign_token = parser->current;

    CMKASTNodeId assign_lhs = CMK_AST_NODE_NIL;
    if (assign_token.type == CMK_TOK_IDENT)
    {
        CMKASTStringId name = parser_new_string(
            parser, assign_token.text, assign_token.len
        );
        if (name == CMK_AST_STRING_NIL)
            return CMK_AST_NODE_NIL;
        CMKSourceSpan span = parser_token_span(&assign_token);
        assign_lhs = parser_new_node(parser, CMK_AST_IDENT, &span);
        if (assign_lhs == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
        CMKASTNode *assign_node = cmk_ast_node_mut(parser->ast, assign_lhs);
        if (!assign_node)
            return CMK_AST_NODE_NIL;
        assign_node->as.ident.name = name;
    }
    else
    {
        CMKSourceSpan span = parser_token_span(&assign_token);
        assign_lhs = parser_new_node(parser, CMK_AST_DISCARD, &span);
        if (assign_lhs == CMK_AST_NODE_NIL)
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
    if (op == CMK_ASSIGN_ADD && assign_token.type == CMK_TOK_UNDERSCORE)
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
    if (!parser_append_child(parser, node, assign_lhs) ||
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
 * 控制流：显式语句帧栈
 * ========================================================================== */

typedef enum cmk_stmt_frame_kind {
    CMK_STMT_FRAME_BLOCK = 0,
    CMK_STMT_FRAME_IF,
    CMK_STMT_FRAME_WHILE,
    CMK_STMT_FRAME_FOR
} CMKStmtFrameKind;

typedef enum cmk_stmt_frame_stage {
    CMK_STMT_STAGE_ITEMS = 0,
    CMK_STMT_STAGE_THEN,
    CMK_STMT_STAGE_ELSE,
    CMK_STMT_STAGE_BODY
} CMKStmtFrameStage;

typedef struct cmk_stmt_frame {
    CMKStmtFrameKind kind;
    CMKStmtFrameStage stage;
    CMKToken start;
    CMKASTNodeId node;
    CMKASTNodeId condition;
    CMKASTNodeId first;
    CMKASTNodeId second;
    CMKASTNodeId type;
    CMKASTNodeId iterable;
    CMKASTStringId name;
    CCBOOL has_else;
} CMKStmtFrame;

static CCBOOL parser_stmt_frame_begin(
    CMKParser *parser, CMKStmtFrame *frame, CMKASTNodeId *simple
)
{
    memset(frame, 0, sizeof(*frame));
    *simple = CMK_AST_NODE_NIL;
    frame->start = parser->current;

    switch (parser->current.type)
    {
    case CMK_TOK_LBRACE:
    {
        CMKToken start = parser->current;
        if (!parser_expect(parser, CMK_TOK_LBRACE, "左花括号"))
            return CCFALSE;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        CMKASTNodeId block = parser_new_node(
            parser, CMK_AST_BLOCK, NULL
        );
        if (block == CMK_AST_NODE_NIL)
        {
            parser_leave_syntax(parser);
            return CCFALSE;
        }
        frame->kind = CMK_STMT_FRAME_BLOCK;
        frame->stage = CMK_STMT_STAGE_ITEMS;
        frame->start = start;
        frame->node = block;
        return CCTRUE;
    }

    case CMK_TOK_KW_IF:
    {
        CMKToken start = parser->current;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        if (!parser_expect(parser, CMK_TOK_KW_IF, "`if`") ||
            !parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
            goto IfFailed;
        CMKASTNodeId condition = parser_parse_expression(parser);
        if (condition == CMK_AST_NODE_NIL ||
            !parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
            goto IfFailed;
        frame->kind = CMK_STMT_FRAME_IF;
        frame->stage = CMK_STMT_STAGE_THEN;
        frame->start = start;
        frame->condition = condition;
        return CCTRUE;

    IfFailed:
        parser_leave_syntax(parser);
        return CCFALSE;
    }

    case CMK_TOK_KW_WHILE:
    {
        CMKToken start = parser->current;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        if (!parser_expect(parser, CMK_TOK_KW_WHILE, "`while`") ||
            !parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
            goto WhileFailed;
        CMKASTNodeId condition = parser_parse_expression(parser);
        if (condition == CMK_AST_NODE_NIL ||
            !parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
            goto WhileFailed;
        frame->kind = CMK_STMT_FRAME_WHILE;
        frame->stage = CMK_STMT_STAGE_BODY;
        frame->start = start;
        frame->condition = condition;
        return CCTRUE;

    WhileFailed:
        parser_leave_syntax(parser);
        return CCFALSE;
    }

    case CMK_TOK_KW_FOR:
    {
        CMKToken start = parser->current;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        if (!parser_expect(parser, CMK_TOK_KW_FOR, "`for`") ||
            !parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
            goto ForFailed;
        CMKASTNodeId type = parser_parse_type(parser);
        if (type == CMK_AST_NODE_NIL)
            goto ForFailed;
        CMKASTStringId name = CMK_AST_STRING_NIL;
        if (!parser_take_identifier(parser, &name))
            goto ForFailed;
        if (!parser_expect(parser, CMK_TOK_KW_IN, "`in`"))
            goto ForFailed;
        CMKASTNodeId iterable = parser_parse_expression(parser);
        if (iterable == CMK_AST_NODE_NIL ||
            !parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
            goto ForFailed;
        frame->kind = CMK_STMT_FRAME_FOR;
        frame->stage = CMK_STMT_STAGE_BODY;
        frame->start = start;
        frame->type = type;
        frame->name = name;
        frame->iterable = iterable;
        return CCTRUE;

    ForFailed:
        parser_leave_syntax(parser);
        return CCFALSE;
    }

    case CMK_TOK_KW_RETURN:
        *simple = parser_parse_return(parser);
        return (*simple != CMK_AST_NODE_NIL) ? CCTRUE : CCFALSE;

    case CMK_TOK_KW_BREAK:
        *simple = parser_parse_break_continue(
            parser, CMK_TOK_KW_BREAK, CMK_AST_BREAK, "`break`"
        );
        return (*simple != CMK_AST_NODE_NIL) ? CCTRUE : CCFALSE;

    case CMK_TOK_KW_CONTINUE:
        *simple = parser_parse_break_continue(
            parser, CMK_TOK_KW_CONTINUE, CMK_AST_CONTINUE, "`continue`"
        );
        return (*simple != CMK_AST_NODE_NIL) ? CCTRUE : CCFALSE;

    case CMK_TOK_IDENT:
    case CMK_TOK_UNDERSCORE:
    {
        CMKToken next;
        memset(&next, 0, sizeof(next));
        if (!parser_peek(parser, &next))
            return CCFALSE;
        if (next.type == CMK_TOK_ASSIGN ||
            next.type == CMK_TOK_PLUS_ASSIGN)
            *simple = parser_parse_assignment(parser);
        else
            *simple = parser_parse_expression_statement(parser);
        return (*simple != CMK_AST_NODE_NIL) ? CCTRUE : CCFALSE;
    }

    default:
        *simple = parser_parse_expression_statement(parser);
        return (*simple != CMK_AST_NODE_NIL) ? CCTRUE : CCFALSE;
    }
}

static CCBOOL parser_stmt_frame_push(
    CMKParser *parser, CCDyn *frames, const CMKStmtFrame *frame
)
{
    if (!cmk_dyn_push(
            frames, (CCUINT32)sizeof(CMKStmtFrame),
            (CCINT32)sizeof(CMKStmtFrame), frame
        ))
    {
        (void)parser_error_current(parser, "内存分配失败");
        return CCFALSE;
    }
    return CCTRUE;
}

static CMKStmtFrame *parser_stmt_frame_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(CMKStmtFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKStmtFrame);
    return &((CMKStmtFrame *)frames->array)[count - 1];
}

static CCBOOL parser_stmt_frame_pop(
    CCDyn *frames, CMKStmtFrame *out
)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(CMKStmtFrame),
        (CCINT32)sizeof(CMKStmtFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void parser_stmt_frames_abort(CMKParser *parser, CCDyn *frames)
{
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKStmtFrame);
    for (CCUINT32 i = 0; i < count; i++)
        parser_leave_syntax(parser);
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL parser_stmt_finalize(
    CMKParser *parser, CMKStmtFrame *frame, CMKASTNodeId *completed
)
{
    switch (frame->kind)
    {
    case CMK_STMT_FRAME_BLOCK:
    {
        if (!parser_expect(parser, CMK_TOK_RBRACE, "右花括号"))
            return CCFALSE;
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        if (!parser_set_node_span(parser, frame->node, &span))
            return CCFALSE;
        *completed = frame->node;
        break;
    }

    case CMK_STMT_FRAME_IF:
    {
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_IF, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CCFALSE;
        CMKASTNode *if_node = cmk_ast_node_mut(parser->ast, node);
        if (!if_node)
            return CCFALSE;
        if_node->as.if_stmt.has_else = frame->has_else;
        if_node->as.if_stmt.is_top_level = CCFALSE;
        if (!parser_append_child(parser, node, frame->condition) ||
            !parser_append_child(parser, node, frame->first) ||
            (frame->has_else &&
             !parser_append_child(parser, node, frame->second)))
            return CCFALSE;
        *completed = node;
        break;
    }

    case CMK_STMT_FRAME_WHILE:
    {
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_WHILE, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CCFALSE;
        if (!parser_append_child(parser, node, frame->condition) ||
            !parser_append_child(parser, node, frame->first))
            return CCFALSE;
        *completed = node;
        break;
    }

    case CMK_STMT_FRAME_FOR:
    {
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_FOR_IN, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CCFALSE;
        CMKASTNode *for_node = cmk_ast_node_mut(parser->ast, node);
        if (!for_node)
            return CCFALSE;
        for_node->as.for_in.name = frame->name;
        if (!parser_append_child(parser, node, frame->type) ||
            !parser_append_child(parser, node, frame->iterable) ||
            !parser_append_child(parser, node, frame->first))
            return CCFALSE;
        *completed = node;
        break;
    }

    default:
        return CCFALSE;
    }

    parser_leave_syntax(parser);
    return CCTRUE;
}

static CMKASTNodeId parser_parse_statement_tree(CMKParser *parser)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CMKASTNodeId completed = CMK_AST_NODE_NIL;

    for (;;)
    {
        if (completed != CMK_AST_NODE_NIL)
        {
            CMKStmtFrame *frame = parser_stmt_frame_top(&frames);
            if (!frame)
            {
                ccalloc(frames.array, 0, NULL);
                return completed;
            }

            if (frame->kind == CMK_STMT_FRAME_BLOCK)
            {
                if (!parser_append_child(parser, frame->node, completed))
                    goto Failed;
                completed = CMK_AST_NODE_NIL;
                continue;
            }

            if (frame->kind == CMK_STMT_FRAME_IF)
            {
                if (frame->stage == CMK_STMT_STAGE_THEN)
                {
                    frame->first = completed;
                    completed = CMK_AST_NODE_NIL;
                    if (parser->current.type == CMK_TOK_KW_ELSE)
                    {
                        if (!parser_advance(parser))
                            goto Failed;
                        frame->stage = CMK_STMT_STAGE_ELSE;
                        continue;
                    }
                }
                else
                {
                    frame->second = completed;
                    frame->has_else = CCTRUE;
                    completed = CMK_AST_NODE_NIL;
                }
            }
            else
            {
                frame->first = completed;
                completed = CMK_AST_NODE_NIL;
            }

            CMKStmtFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!parser_stmt_finalize(parser, frame, &completed) ||
                !parser_stmt_frame_pop(&frames, &popped))
                goto Failed;
            continue;
        }

        if (frames.size == 0)
        {
            CMKStmtFrame start;
            CMKASTNodeId simple = CMK_AST_NODE_NIL;
            if (!parser_stmt_frame_begin(parser, &start, &simple))
                goto Failed;
            if (simple != CMK_AST_NODE_NIL)
            {
                ccalloc(frames.array, 0, NULL);
                return simple;
            }
            if (!parser_stmt_frame_push(parser, &frames, &start))
            {
                parser_leave_syntax(parser);
                goto Failed;
            }
            continue;
        }

        CMKStmtFrame *frame = parser_stmt_frame_top(&frames);
        if (!frame)
            goto Failed;

        if (frame->kind == CMK_STMT_FRAME_BLOCK)
        {
            if (parser->current.type == CMK_TOK_RBRACE)
            {
                if (!parser_stmt_finalize(parser, frame, &completed))
                    goto Failed;
                CMKStmtFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!parser_stmt_frame_pop(&frames, &popped))
                    goto Failed;
                continue;
            }

            if (parser->current.type == CMK_TOK_KW_CONST ||
                parser_is_type_start(parser))
            {
                CMKASTNodeId item = parser_parse_var_decl(parser);
                if (item == CMK_AST_NODE_NIL ||
                    !parser_append_child(parser, frame->node, item))
                    goto Failed;
                continue;
            }
        }

        CMKStmtFrame child;
        CMKASTNodeId simple = CMK_AST_NODE_NIL;
        if (!parser_stmt_frame_begin(parser, &child, &simple))
            goto Failed;
        if (simple != CMK_AST_NODE_NIL)
        {
            completed = simple;
            continue;
        }
        if (!parser_stmt_frame_push(parser, &frames, &child))
        {
            parser_leave_syntax(parser);
            goto Failed;
        }
    }

Failed:
    parser_stmt_frames_abort(parser, &frames);
    return CMK_AST_NODE_NIL;
}

CMKASTNodeId parser_parse_block(CMKParser *parser)
{
    if (!parser || parser->failed)
        return CMK_AST_NODE_NIL;
    if (parser->current.type != CMK_TOK_LBRACE)
    {
        (void)parser_error_current(parser, "期望左花括号");
        return CMK_AST_NODE_NIL;
    }
    return parser_parse_statement_tree(parser);
}

CMKASTNodeId parser_parse_statement(CMKParser *parser)
{
    if (!parser || parser->failed)
        return CMK_AST_NODE_NIL;
    return parser_parse_statement_tree(parser);
}
