/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 语句 Parser：显式语句帧栈、块、if/while/for、return、赋值与表达式语句
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_parser_internal.h"
#include "mk_safe.h"

#include <ccalloc.h>

#include <string.h>

/* ============================================================================
 * 简单语句
 * ========================================================================== */

static MKASTNodeId parser_parse_return(MKParser *parser)
{
    MKToken start = parser->current;
    if (!parser_expect(parser, MK_TOK_KW_RETURN, "`return`"))
        return MK_AST_NODE_NIL;

    CCBOOL has_value = CCFALSE;
    MKASTNodeId value = MK_AST_NODE_NIL;
    if (parser->current.type != MK_TOK_SEMI)
    {
        has_value = CCTRUE;
        value = parser_parse_expression(parser);
        if (value == MK_AST_NODE_NIL)
            return MK_AST_NODE_NIL;
    }
    if (!parser_expect(parser, MK_TOK_SEMI, "分号"))
        return MK_AST_NODE_NIL;

    MKSourceSpan span = parser_make_span(&start, &parser->previous);
    MKASTNodeId node = parser_new_node(
        parser, MK_AST_RETURN, &span
    );
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;

    MKASTNode *return_node = mk_ast_node_mut(parser->ast, node);
    if (!return_node)
        return MK_AST_NODE_NIL;
    return_node->as.return_stmt.has_value = has_value;
    if (has_value && !parser_append_child(parser, node, value))
        return MK_AST_NODE_NIL;
    return node;
}

static MKASTNodeId parser_parse_break_continue(
    MKParser *parser, MKTokenType type, MKASTKind kind, const char *what
)
{
    MKToken start = parser->current;
    if (!parser_expect(parser, type, what))
        return MK_AST_NODE_NIL;
    if (!parser_expect(parser, MK_TOK_SEMI, "分号"))
        return MK_AST_NODE_NIL;

    MKSourceSpan span = parser_make_span(&start, &parser->previous);
    return parser_new_node(parser, kind, &span);
}

static MKASTNodeId parser_parse_assignment(MKParser *parser)
{
    MKToken start = parser->current;
    MKToken assign_token = parser->current;

    MKASTNodeId assign_lhs = MK_AST_NODE_NIL;
    if (assign_token.type == MK_TOK_IDENT)
    {
        MKASTStringId name = parser_new_string(
            parser, assign_token.text, assign_token.len
        );
        if (name == MK_AST_STRING_NIL)
            return MK_AST_NODE_NIL;
        MKSourceSpan span = parser_token_span(&assign_token);
        assign_lhs = parser_new_node(parser, MK_AST_IDENT, &span);
        if (assign_lhs == MK_AST_NODE_NIL)
            return MK_AST_NODE_NIL;
        MKASTNode *assign_node = mk_ast_node_mut(parser->ast, assign_lhs);
        if (!assign_node)
            return MK_AST_NODE_NIL;
        assign_node->as.ident.name = name;
    }
    else
    {
        MKSourceSpan span = parser_token_span(&assign_token);
        assign_lhs = parser_new_node(parser, MK_AST_DISCARD, &span);
        if (assign_lhs == MK_AST_NODE_NIL)
            return MK_AST_NODE_NIL;
    }

    if (!parser_advance(parser))
        return MK_AST_NODE_NIL;

    MKAssignOp op;
    if (parser->current.type == MK_TOK_ASSIGN)
        op = MK_ASSIGN_SET;
    else if (parser->current.type == MK_TOK_PLUS_ASSIGN)
        op = MK_ASSIGN_ADD;
    else
    {
        (void)parser_error_current(
            parser, "赋值语句需要 `=` 或 `+=`"
        );
        return MK_AST_NODE_NIL;
    }
    if (op == MK_ASSIGN_ADD && assign_token.type == MK_TOK_UNDERSCORE)
    {
        (void)parser_error_current(parser, "`_` 不能使用 `+=`");
        return MK_AST_NODE_NIL;
    }
    if (!parser_advance(parser))
        return MK_AST_NODE_NIL;

    MKASTNodeId value = parser_parse_expression(parser);
    if (value == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    if (!parser_expect(parser, MK_TOK_SEMI, "分号"))
        return MK_AST_NODE_NIL;

    MKSourceSpan span = parser_make_span(&start, &parser->previous);
    MKASTNodeId node = parser_new_node(parser, MK_AST_ASSIGN, &span);
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    MKASTNode *assign = mk_ast_node_mut(parser->ast, node);
    if (!assign)
        return MK_AST_NODE_NIL;
    assign->as.assign.op = op;
    if (!parser_append_child(parser, node, assign_lhs) ||
        !parser_append_child(parser, node, value))
        return MK_AST_NODE_NIL;
    return node;
}

static MKASTNodeId parser_parse_expression_statement(MKParser *parser)
{
    MKToken start = parser->current;
    MKASTNodeId expression = parser_parse_expression(parser);
    if (expression == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    if (!parser_expect(parser, MK_TOK_SEMI, "分号"))
        return MK_AST_NODE_NIL;

    MKSourceSpan span = parser_make_span(&start, &parser->previous);
    MKASTNodeId node = parser_new_node(
        parser, MK_AST_EXPR_STMT, &span
    );
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    if (!parser_append_child(parser, node, expression))
        return MK_AST_NODE_NIL;
    return node;
}

/* ============================================================================
 * 控制流：显式语句帧栈
 * ========================================================================== */

typedef enum mk_stmt_frame_kind {
    MK_STMT_FRAME_BLOCK = 0,
    MK_STMT_FRAME_IF,
    MK_STMT_FRAME_WHILE,
    MK_STMT_FRAME_FOR
} MKStmtFrameKind;

typedef enum mk_stmt_frame_stage {
    MK_STMT_STAGE_ITEMS = 0,
    MK_STMT_STAGE_THEN,
    MK_STMT_STAGE_ELSE,
    MK_STMT_STAGE_BODY
} MKStmtFrameStage;

typedef struct mk_stmt_frame {
    MKStmtFrameKind kind;
    MKStmtFrameStage stage;
    MKToken start;
    MKASTNodeId node;
    MKASTNodeId condition;
    MKASTNodeId first;
    MKASTNodeId second;
    MKASTNodeId type;
    MKASTNodeId iterable;
    MKASTStringId name;
    CCBOOL has_else;
} MKStmtFrame;

static CCBOOL parser_stmt_frame_begin(
    MKParser *parser, MKStmtFrame *frame, MKASTNodeId *simple
)
{
    memset(frame, 0, sizeof(*frame));
    *simple = MK_AST_NODE_NIL;
    frame->start = parser->current;

    switch (parser->current.type)
    {
    case MK_TOK_LBRACE:
    {
        MKToken start = parser->current;
        if (!parser_expect(parser, MK_TOK_LBRACE, "左花括号"))
            return CCFALSE;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        MKASTNodeId block = parser_new_node(
            parser, MK_AST_BLOCK, NULL
        );
        if (block == MK_AST_NODE_NIL)
        {
            parser_leave_syntax(parser);
            return CCFALSE;
        }
        frame->kind = MK_STMT_FRAME_BLOCK;
        frame->stage = MK_STMT_STAGE_ITEMS;
        frame->start = start;
        frame->node = block;
        return CCTRUE;
    }

    case MK_TOK_KW_IF:
    {
        MKToken start = parser->current;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        if (!parser_expect(parser, MK_TOK_KW_IF, "`if`") ||
            !parser_expect(parser, MK_TOK_LPAREN, "左圆括号"))
            goto IfFailed;
        MKASTNodeId condition = parser_parse_expression(parser);
        if (condition == MK_AST_NODE_NIL ||
            !parser_expect(parser, MK_TOK_RPAREN, "右圆括号"))
            goto IfFailed;
        frame->kind = MK_STMT_FRAME_IF;
        frame->stage = MK_STMT_STAGE_THEN;
        frame->start = start;
        frame->condition = condition;
        return CCTRUE;

    IfFailed:
        parser_leave_syntax(parser);
        return CCFALSE;
    }

    case MK_TOK_KW_WHILE:
    {
        MKToken start = parser->current;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        if (!parser_expect(parser, MK_TOK_KW_WHILE, "`while`") ||
            !parser_expect(parser, MK_TOK_LPAREN, "左圆括号"))
            goto WhileFailed;
        MKASTNodeId condition = parser_parse_expression(parser);
        if (condition == MK_AST_NODE_NIL ||
            !parser_expect(parser, MK_TOK_RPAREN, "右圆括号"))
            goto WhileFailed;
        frame->kind = MK_STMT_FRAME_WHILE;
        frame->stage = MK_STMT_STAGE_BODY;
        frame->start = start;
        frame->condition = condition;
        return CCTRUE;

    WhileFailed:
        parser_leave_syntax(parser);
        return CCFALSE;
    }

    case MK_TOK_KW_FOR:
    {
        MKToken start = parser->current;
        if (!parser_enter_syntax(parser))
            return CCFALSE;
        if (!parser_expect(parser, MK_TOK_KW_FOR, "`for`") ||
            !parser_expect(parser, MK_TOK_LPAREN, "左圆括号"))
            goto ForFailed;
        MKASTNodeId type = parser_parse_type(parser);
        if (type == MK_AST_NODE_NIL)
            goto ForFailed;
        MKASTStringId name = MK_AST_STRING_NIL;
        if (!parser_take_identifier(parser, &name))
            goto ForFailed;
        if (!parser_expect(parser, MK_TOK_KW_IN, "`in`"))
            goto ForFailed;
        MKASTNodeId iterable = parser_parse_expression(parser);
        if (iterable == MK_AST_NODE_NIL ||
            !parser_expect(parser, MK_TOK_RPAREN, "右圆括号"))
            goto ForFailed;
        frame->kind = MK_STMT_FRAME_FOR;
        frame->stage = MK_STMT_STAGE_BODY;
        frame->start = start;
        frame->type = type;
        frame->name = name;
        frame->iterable = iterable;
        return CCTRUE;

    ForFailed:
        parser_leave_syntax(parser);
        return CCFALSE;
    }

    case MK_TOK_KW_RETURN:
        *simple = parser_parse_return(parser);
        return (*simple != MK_AST_NODE_NIL) ? CCTRUE : CCFALSE;

    case MK_TOK_KW_BREAK:
        *simple = parser_parse_break_continue(
            parser, MK_TOK_KW_BREAK, MK_AST_BREAK, "`break`"
        );
        return (*simple != MK_AST_NODE_NIL) ? CCTRUE : CCFALSE;

    case MK_TOK_KW_CONTINUE:
        *simple = parser_parse_break_continue(
            parser, MK_TOK_KW_CONTINUE, MK_AST_CONTINUE, "`continue`"
        );
        return (*simple != MK_AST_NODE_NIL) ? CCTRUE : CCFALSE;

    case MK_TOK_IDENT:
    case MK_TOK_UNDERSCORE:
    {
        MKToken next;
        memset(&next, 0, sizeof(next));
        if (!parser_peek(parser, &next))
            return CCFALSE;
        if (next.type == MK_TOK_ASSIGN ||
            next.type == MK_TOK_PLUS_ASSIGN)
            *simple = parser_parse_assignment(parser);
        else
            *simple = parser_parse_expression_statement(parser);
        return (*simple != MK_AST_NODE_NIL) ? CCTRUE : CCFALSE;
    }

    default:
        *simple = parser_parse_expression_statement(parser);
        return (*simple != MK_AST_NODE_NIL) ? CCTRUE : CCFALSE;
    }
}

static CCBOOL parser_stmt_frame_push(
    MKParser *parser, CCDyn *frames, const MKStmtFrame *frame
)
{
    if (!mk_dyn_push(
            frames, (CCUINT32)sizeof(MKStmtFrame),
            (CCINT32)sizeof(MKStmtFrame), frame
        ))
    {
        (void)parser_error_current(parser, "内存分配失败");
        return CCFALSE;
    }
    return CCTRUE;
}

static MKStmtFrame *parser_stmt_frame_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(MKStmtFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKStmtFrame);
    return &((MKStmtFrame *)frames->array)[count - 1];
}

static CCBOOL parser_stmt_frame_pop(
    CCDyn *frames, MKStmtFrame *out
)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(MKStmtFrame),
        (CCINT32)sizeof(MKStmtFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void parser_stmt_frames_abort(MKParser *parser, CCDyn *frames)
{
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKStmtFrame);
    for (CCUINT32 i = 0; i < count; i++)
        parser_leave_syntax(parser);
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL parser_stmt_finalize(
    MKParser *parser, MKStmtFrame *frame, MKASTNodeId *completed
)
{
    switch (frame->kind)
    {
    case MK_STMT_FRAME_BLOCK:
    {
        if (!parser_expect(parser, MK_TOK_RBRACE, "右花括号"))
            return CCFALSE;
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        if (!parser_set_node_span(parser, frame->node, &span))
            return CCFALSE;
        *completed = frame->node;
        break;
    }

    case MK_STMT_FRAME_IF:
    {
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_IF, &span
        );
        if (node == MK_AST_NODE_NIL)
            return CCFALSE;
        MKASTNode *if_node = mk_ast_node_mut(parser->ast, node);
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

    case MK_STMT_FRAME_WHILE:
    {
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_WHILE, &span
        );
        if (node == MK_AST_NODE_NIL)
            return CCFALSE;
        if (!parser_append_child(parser, node, frame->condition) ||
            !parser_append_child(parser, node, frame->first))
            return CCFALSE;
        *completed = node;
        break;
    }

    case MK_STMT_FRAME_FOR:
    {
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_FOR_IN, &span
        );
        if (node == MK_AST_NODE_NIL)
            return CCFALSE;
        MKASTNode *for_node = mk_ast_node_mut(parser->ast, node);
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

static MKASTNodeId parser_parse_statement_tree(MKParser *parser)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    MKASTNodeId completed = MK_AST_NODE_NIL;

    for (;;)
    {
        if (completed != MK_AST_NODE_NIL)
        {
            MKStmtFrame *frame = parser_stmt_frame_top(&frames);
            if (!frame)
            {
                ccalloc(frames.array, 0, NULL);
                return completed;
            }

            if (frame->kind == MK_STMT_FRAME_BLOCK)
            {
                if (!parser_append_child(parser, frame->node, completed))
                    goto Failed;
                completed = MK_AST_NODE_NIL;
                continue;
            }

            if (frame->kind == MK_STMT_FRAME_IF)
            {
                if (frame->stage == MK_STMT_STAGE_THEN)
                {
                    frame->first = completed;
                    completed = MK_AST_NODE_NIL;
                    if (parser->current.type == MK_TOK_KW_ELSE)
                    {
                        if (!parser_advance(parser))
                            goto Failed;
                        frame->stage = MK_STMT_STAGE_ELSE;
                        continue;
                    }
                }
                else
                {
                    frame->second = completed;
                    frame->has_else = CCTRUE;
                    completed = MK_AST_NODE_NIL;
                }
            }
            else
            {
                frame->first = completed;
                completed = MK_AST_NODE_NIL;
            }

            MKStmtFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!parser_stmt_finalize(parser, frame, &completed) ||
                !parser_stmt_frame_pop(&frames, &popped))
                goto Failed;
            continue;
        }

        if (frames.size == 0)
        {
            MKStmtFrame start;
            MKASTNodeId simple = MK_AST_NODE_NIL;
            if (!parser_stmt_frame_begin(parser, &start, &simple))
                goto Failed;
            if (simple != MK_AST_NODE_NIL)
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

        MKStmtFrame *frame = parser_stmt_frame_top(&frames);
        if (!frame)
            goto Failed;

        if (frame->kind == MK_STMT_FRAME_BLOCK)
        {
            if (parser->current.type == MK_TOK_RBRACE)
            {
                if (!parser_stmt_finalize(parser, frame, &completed))
                    goto Failed;
                MKStmtFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!parser_stmt_frame_pop(&frames, &popped))
                    goto Failed;
                continue;
            }

            if (parser->current.type == MK_TOK_KW_CONST ||
                parser_is_type_start(parser))
            {
                MKASTNodeId item = parser_parse_var_decl(parser);
                if (item == MK_AST_NODE_NIL ||
                    !parser_append_child(parser, frame->node, item))
                    goto Failed;
                continue;
            }
        }

        MKStmtFrame child;
        MKASTNodeId simple = MK_AST_NODE_NIL;
        if (!parser_stmt_frame_begin(parser, &child, &simple))
            goto Failed;
        if (simple != MK_AST_NODE_NIL)
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
    return MK_AST_NODE_NIL;
}

MKASTNodeId parser_parse_block(MKParser *parser)
{
    if (!parser || parser->failed)
        return MK_AST_NODE_NIL;
    if (parser->current.type != MK_TOK_LBRACE)
    {
        (void)parser_error_current(parser, "期望左花括号");
        return MK_AST_NODE_NIL;
    }
    return parser_parse_statement_tree(parser);
}

MKASTNodeId parser_parse_statement(MKParser *parser)
{
    if (!parser || parser->failed)
        return MK_AST_NODE_NIL;
    return parser_parse_statement_tree(parser);
}
