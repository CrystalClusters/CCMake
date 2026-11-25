/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 声明 Parser：模块、导入、函数与顶层声明
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_parser_internal.h"
#include "mk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <string.h>

static MKASTNodeId parser_parse_include(MKParser *parser);
static MKASTNodeId parser_parse_import(MKParser *parser);
static MKASTNodeId parser_parse_func_decl(MKParser *parser);
static MKASTNodeId parser_parse_top_if(MKParser *parser);

/* ============================================================================
 * 类型
 * ========================================================================== */

CCBOOL parser_is_type_start(MKParser *parser)
{
    if (!parser)
        return CCFALSE;
    switch (parser->current.type)
    {
    case MK_TOK_KW_STRING:
    case MK_TOK_KW_INT:
    case MK_TOK_KW_BOOL:
    case MK_TOK_KW_FLOAT:
    case MK_TOK_KW_LIST:
        return CCTRUE;

    default:
        return CCFALSE;
    }
}

static MKASTNodeId parser_make_scalar_type(
    MKParser *parser, MKTypeKind kind
)
{
    MKToken token = parser->current;
    MKSourceSpan span = parser_token_span(&token);
    MKASTNodeId node = parser_new_node(parser, MK_AST_TYPE, &span);
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;

    MKASTNode *type_node = mk_ast_node_mut(parser->ast, node);
    if (!type_node)
        return MK_AST_NODE_NIL;
    type_node->as.type.kind = kind;

    if (!parser_advance(parser))
        return MK_AST_NODE_NIL;
    return node;
}

MKASTNodeId parser_parse_type(MKParser *parser)
{
    if (!parser || parser->failed)
        return MK_AST_NODE_NIL;

    CCDyn layers;
    memset(&layers, 0, sizeof(layers));
    CCUINT32 entered = 0;
    MKASTNodeId base = MK_AST_NODE_NIL;
    CCUINT32 layer_count = 0;

    while (parser->current.type == MK_TOK_KW_LIST)
    {
        if (!parser_enter_syntax(parser))
            goto Failed;
        entered++;
        MKToken start = parser->current;
        if (!mk_dyn_push(
                &layers, (CCUINT32)sizeof(MKToken),
                (CCINT32)sizeof(MKToken), &start
            ))
        {
            (void)parser_error_current(parser, "内存分配失败");
            goto Failed;
        }
        layer_count++;
        if (!parser_advance(parser))
            goto Failed;
        if (!parser_expect(parser, MK_TOK_LT, "`<`"))
            goto Failed;
    }

    if (!parser_enter_syntax(parser))
        goto Failed;
    entered++;

    switch (parser->current.type)
    {
    case MK_TOK_KW_STRING:
        base = parser_make_scalar_type(parser, MK_TYPE_STRING);
        break;

    case MK_TOK_KW_INT:
        base = parser_make_scalar_type(parser, MK_TYPE_INT);
        break;

    case MK_TOK_KW_BOOL:
        base = parser_make_scalar_type(parser, MK_TYPE_BOOL);
        break;

    case MK_TOK_KW_FLOAT:
        base = parser_make_scalar_type(parser, MK_TYPE_FLOAT);
        break;

    default:
        (void)parser_error_current(
            parser, "期望类型，实际%s",
            mk_token_type_name(parser->current.type)
        );
        break;
    }
    parser_leave_syntax(parser);
    entered--;
    if (base == MK_AST_NODE_NIL)
        goto Failed;

    for (CCUINT32 i = layer_count; i > 0; i--)
    {
        if (!parser_expect(parser, MK_TOK_GT, "`>`"))
            goto Failed;

        MKToken *starts = (MKToken *)layers.array;
        MKSourceSpan span =
            parser_make_span(&starts[i - 1], &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_TYPE, &span
        );
        if (node == MK_AST_NODE_NIL)
            goto Failed;
        MKASTNode *type_node = mk_ast_node_mut(parser->ast, node);
        if (!type_node)
            goto Failed;
        type_node->as.type.kind = MK_TYPE_LIST;
        if (!parser_append_child(parser, node, base))
            goto Failed;
        base = node;
        parser_leave_syntax(parser);
        entered--;
    }

    ccalloc(layers.array, 0, NULL);
    return base;

Failed:
    for (CCUINT32 i = 0; i < entered; i++)
        parser_leave_syntax(parser);
    if (layers.array)
        ccalloc(layers.array, 0, NULL);
    return MK_AST_NODE_NIL;
}

MKASTNodeId parser_parse_return_type(MKParser *parser)
{
    if (!parser || parser->failed)
        return MK_AST_NODE_NIL;
    if (parser->current.type == MK_TOK_KW_VOID)
        return parser_make_scalar_type(parser, MK_TYPE_VOID);
    return parser_parse_type(parser);
}

/* ============================================================================
 * 变量声明
 * ========================================================================== */

MKASTNodeId parser_parse_var_decl(MKParser *parser)
{
    MKToken start = parser->current;
    CCBOOL is_const = CCFALSE;
    if (parser->current.type == MK_TOK_KW_CONST)
    {
        is_const = CCTRUE;
        if (!parser_advance(parser))
            return MK_AST_NODE_NIL;
    }

    MKASTNodeId type = parser_parse_type(parser);
    if (type == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;

    MKASTStringId name = MK_AST_STRING_NIL;
    if (!parser_take_identifier(parser, &name))
        return MK_AST_NODE_NIL;
    if (!parser_expect(parser, MK_TOK_ASSIGN, "`=`"))
        return MK_AST_NODE_NIL;

    MKASTNodeId value = parser_parse_expression(parser);
    if (value == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    if (!parser_expect(parser, MK_TOK_SEMI, "分号"))
        return MK_AST_NODE_NIL;

    MKSourceSpan span = parser_make_span(&start, &parser->previous);
    MKASTNodeId node = parser_new_node(
        parser, MK_AST_VAR_DECL, &span
    );
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    MKASTNode *var_node = mk_ast_node_mut(parser->ast, node);
    if (!var_node)
        return MK_AST_NODE_NIL;
    var_node->as.var_decl.name = name;
    var_node->as.var_decl.is_const = is_const;
    if (!parser_append_child(parser, node, type) ||
        !parser_append_child(parser, node, value))
        return MK_AST_NODE_NIL;
    return node;
}

/* ============================================================================
 * include / import
 * ========================================================================== */

static MKASTNodeId parser_parse_include(MKParser *parser)
{
    MKToken start = parser->current;
    if (!parser_expect(parser, MK_TOK_AT_INCLUDE, "`@include`"))
        return MK_AST_NODE_NIL;

    MKASTStringId module = MK_AST_STRING_NIL;
    if (!parser_take_string(parser, &module))
        return MK_AST_NODE_NIL;
    if (!parser_expect(parser, MK_TOK_SEMI, "分号"))
        return MK_AST_NODE_NIL;

    MKSourceSpan span = parser_make_span(&start, &parser->previous);
    MKASTNodeId node = parser_new_node(
        parser, MK_AST_INCLUDE_DECL, &span
    );
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    MKASTNode *include_node = mk_ast_node_mut(parser->ast, node);
    if (!include_node)
        return MK_AST_NODE_NIL;
    include_node->as.include_decl.module = module;
    return node;
}

static MKASTNodeId parser_make_import_item(
    MKParser *parser, MKToken name_token, MKASTStringId name
)
{
    MKSourceSpan span = parser_token_span(&name_token);
    MKASTNodeId node = parser_new_node(
        parser, MK_AST_IMPORT_ITEM, &span
    );
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    MKASTNode *item = mk_ast_node_mut(parser->ast, node);
    if (!item)
        return MK_AST_NODE_NIL;
    item->as.import_item.name = name;
    item->as.import_item.alias = MK_AST_STRING_NIL;
    return node;
}

static MKASTNodeId parser_parse_import(MKParser *parser)
{
    MKToken start = parser->current;
    if (!parser_expect(parser, MK_TOK_AT_IMPORT, "`@import`"))
        return MK_AST_NODE_NIL;

    CCBOOL is_func = CCTRUE;
    if (parser->current.type != MK_TOK_KW_FUNC)
        return parser_error_current(
            parser, "导入需要 `func`"
        ), MK_AST_NODE_NIL;

    if (!parser_advance(parser))
        return MK_AST_NODE_NIL;

    MKSourceSpan zero_span;
    memset(&zero_span, 0, sizeof(zero_span));
    MKASTNodeId node = parser_new_node(
        parser, MK_AST_IMPORT_DECL, &zero_span
    );
    if (node == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;
    MKASTNode *import_node = mk_ast_node_mut(parser->ast, node);
    if (!import_node)
        return MK_AST_NODE_NIL;
    import_node->as.import_decl.kind = MK_IMPORT_FUNC;
    import_node->as.import_decl.module = MK_AST_STRING_NIL;
    import_node->as.import_decl.alias = MK_AST_STRING_NIL;
    import_node->as.import_decl.wildcard = CCFALSE;

    CCUINT32 item_count = 0;
    MKToken first_item_start;
    memset(&first_item_start, 0, sizeof(first_item_start));
    if (is_func && parser->current.type == MK_TOK_STAR)
    {
        import_node = mk_ast_node_mut(parser->ast, node);
        if (!import_node)
            return MK_AST_NODE_NIL;
        import_node->as.import_decl.wildcard = CCTRUE;
        if (!parser_advance(parser))
            return MK_AST_NODE_NIL;
    }
    else
    {
        for (;;)
        {
            MKToken name_token = parser->current;
            MKASTStringId name = MK_AST_STRING_NIL;
            if (!parser_take_identifier(parser, &name))
                return MK_AST_NODE_NIL;

            MKASTNodeId item = parser_make_import_item(
                parser, name_token, name
            );
            if (item == MK_AST_NODE_NIL)
                return MK_AST_NODE_NIL;
            if (!parser_append_child(parser, node, item))
                return MK_AST_NODE_NIL;
            if (item_count == 0)
                first_item_start = name_token;
            item_count++;

            if (parser->current.type != MK_TOK_COMMA)
                break;
            if (!parser_advance(parser))
                return MK_AST_NODE_NIL;
        }
    }

    if (is_func)
    {
        if (parser->current.type == MK_TOK_KW_AS)
        {
            if (item_count != 1)
                return parser_error_current(
                    parser, "函数批量导入不能使用 `as`"
                ), MK_AST_NODE_NIL;
            if (!parser_advance(parser))
                return MK_AST_NODE_NIL;

            MKASTStringId alias = MK_AST_STRING_NIL;
            if (!parser_take_identifier(parser, &alias))
                return MK_AST_NODE_NIL;

            import_node = mk_ast_node_mut(parser->ast, node);
            if (!import_node)
                return MK_AST_NODE_NIL;
            MKASTNode *item_node = mk_ast_node_mut(
                parser->ast, import_node->first_child
            );
            if (!item_node)
                return MK_AST_NODE_NIL;
            item_node->as.import_item.alias = alias;

            MKSourceSpan item_span = parser_make_span(
                &first_item_start, &parser->previous
            );
            item_node->span = item_span;
        }
    }

    if (!parser_expect(parser, MK_TOK_KW_FROM, "`from`"))
        return MK_AST_NODE_NIL;

    MKASTStringId module = MK_AST_STRING_NIL;
    if (!parser_take_string(parser, &module))
        return MK_AST_NODE_NIL;

    if (!is_func)
    {
        if (!parser_expect(parser, MK_TOK_KW_AS, "`as`"))
            return MK_AST_NODE_NIL;
        MKASTStringId alias = MK_AST_STRING_NIL;
        if (!parser_take_identifier(parser, &alias))
            return MK_AST_NODE_NIL;
        import_node = mk_ast_node_mut(parser->ast, node);
        if (!import_node)
            return MK_AST_NODE_NIL;
        import_node->as.import_decl.alias = alias;
    }

    if (!parser_expect(parser, MK_TOK_SEMI, "分号"))
        return MK_AST_NODE_NIL;

    import_node = mk_ast_node_mut(parser->ast, node);
    if (!import_node)
        return MK_AST_NODE_NIL;
    import_node->as.import_decl.module = module;
    MKSourceSpan span = parser_make_span(&start, &parser->previous);
    if (!parser_set_node_span(parser, node, &span))
        return MK_AST_NODE_NIL;
    return node;
}

/* ============================================================================
 * 函数声明
 * ========================================================================== */

static MKASTNodeId parser_parse_func_decl(MKParser *parser)
{
    MKToken start = parser->current;
    if (!parser_expect(parser, MK_TOK_KW_FUNC, "`func`"))
        return MK_AST_NODE_NIL;

    MKASTStringId name = MK_AST_STRING_NIL;
    if (!parser_take_identifier(parser, &name))
        return MK_AST_NODE_NIL;
    if (!parser_expect(parser, MK_TOK_LPAREN, "左圆括号"))
        return MK_AST_NODE_NIL;

    CCDyn params;
    memset(&params, 0, sizeof(params));
    MKASTNodeId func_id = MK_AST_NODE_NIL;

    if (parser->current.type != MK_TOK_RPAREN)
    {
        for (;;)
        {
            CCUINT32 count =
                params.size / (CCUINT32)sizeof(MKASTNodeId);
            if (parser->limits.max_function_params != 0 &&
                count >= parser->limits.max_function_params)
            {
                (void)parser_error_current(
                    parser, "函数参数数量超过限制"
                );
                goto Failed;
            }

            MKToken param_start = parser->current;
            MKASTNodeId type = parser_parse_type(parser);
            if (type == MK_AST_NODE_NIL)
                goto Failed;

            MKASTStringId param_name = MK_AST_STRING_NIL;
            if (!parser_take_identifier(parser, &param_name))
                goto Failed;

            MKSourceSpan span =
                parser_make_span(&param_start, &parser->previous);
            MKASTNodeId param = parser_new_node(
                parser, MK_AST_PARAM, &span
            );
            if (param == MK_AST_NODE_NIL)
                goto Failed;
            MKASTNode *param_node = mk_ast_node_mut(parser->ast, param);
            if (!param_node)
                goto Failed;
            param_node->as.param.name = param_name;
            if (!parser_append_child(parser, param, type))
                goto Failed;
            if (!mk_dyn_push(
                    &params,
                    (CCUINT32)sizeof(MKASTNodeId),
                    (CCINT32)sizeof(MKASTNodeId),
                    &param
                ))
            {
                (void)parser_error_current(parser, "内存分配失败");
                goto Failed;
            }

            if (parser->current.type != MK_TOK_COMMA)
                break;
            if (!parser_advance(parser))
                goto Failed;
            if (parser->current.type == MK_TOK_RPAREN)
            {
                (void)parser_error_current(
                    parser, "函数参数不允许尾逗号"
                );
                goto Failed;
            }
        }
    }

    if (!parser_expect(parser, MK_TOK_RPAREN, "右圆括号"))
        goto Failed;
    if (!parser_expect(parser, MK_TOK_ARROW, "`->`"))
        goto Failed;

    MKASTNodeId return_type = parser_parse_return_type(parser);
    if (return_type == MK_AST_NODE_NIL)
        goto Failed;
    MKASTNodeId body = parser_parse_block(parser);
    if (body == MK_AST_NODE_NIL)
        goto Failed;

    {
        MKSourceSpan span =
            parser_make_span(&start, &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_FUNC_DECL, &span
        );
        if (node == MK_AST_NODE_NIL)
            goto Failed;
        MKASTNode *func_node = mk_ast_node_mut(parser->ast, node);
        if (!func_node)
            goto Failed;
        func_node->as.func_decl.name = name;
        func_node->as.func_decl.param_count =
            params.size / (CCUINT32)sizeof(MKASTNodeId);
        if (!parser_append_child(parser, node, return_type))
            goto Failed;
        CCUINT32 param_count =
            params.size / (CCUINT32)sizeof(MKASTNodeId);
        for (CCUINT32 i = 0; i < param_count; i++)
        {
            MKASTNodeId param = MK_AST_NODE_NIL;
            if (cc_dyn_core_seek(
                    &params,
                    (CCUINT32)sizeof(MKASTNodeId),
                    (CCINT32)sizeof(MKASTNodeId),
                    &param, i
                ) != 0)
            {
                (void)parser_error_current(parser, "参数表损坏");
                goto Failed;
            }
            if (!parser_append_child(parser, node, param))
                goto Failed;
        }
        if (!parser_append_child(parser, node, body))
            goto Failed;
        func_id = node;
    }

    if (params.array)
        ccalloc(params.array, 0, NULL);
    return func_id;

Failed:
    if (params.array)
        ccalloc(params.array, 0, NULL);
    return MK_AST_NODE_NIL;
}

/* ============================================================================
 * 顶层 if
 * ========================================================================== */

/* ============================================================================
 * 顶层 if：显式分支帧栈
 * ========================================================================== */

typedef enum mk_top_frame_kind {
    MK_TOP_FRAME_IF = 0,
    MK_TOP_FRAME_BLOCK
} MKTopFrameKind;

typedef enum mk_top_frame_stage {
    MK_TOP_STAGE_THEN = 0,
    MK_TOP_STAGE_ELSE,
    MK_TOP_STAGE_ITEMS
} MKTopFrameStage;

typedef struct mk_top_frame {
    MKTopFrameKind kind;
    MKTopFrameStage stage;
    MKToken start;
    MKASTNodeId node;
    MKASTNodeId condition;
    MKASTNodeId first;
    MKASTNodeId second;
    CCBOOL has_else;
} MKTopFrame;

static MKTopFrame *parser_top_frame_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(MKTopFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKTopFrame);
    return &((MKTopFrame *)frames->array)[count - 1];
}

static CCBOOL parser_top_frame_push(
    MKParser *parser, CCDyn *frames, const MKTopFrame *frame
)
{
    if (!mk_dyn_push(
            frames, (CCUINT32)sizeof(MKTopFrame),
            (CCINT32)sizeof(MKTopFrame), frame
        ))
        return parser_error_current(parser, "内存分配失败"), CCFALSE;
    return CCTRUE;
}

static CCBOOL parser_top_frame_pop(CCDyn *frames, MKTopFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(MKTopFrame),
        (CCINT32)sizeof(MKTopFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static CCBOOL parser_top_begin_if(
    MKParser *parser, MKTopFrame *frame
)
{
    memset(frame, 0, sizeof(*frame));
    MKToken start = parser->current;
    if (!parser_enter_syntax(parser))
        return CCFALSE;
    if (!parser_expect(parser, MK_TOK_KW_IF, "`if`") ||
        !parser_expect(parser, MK_TOK_LPAREN, "左圆括号"))
        goto Failed;
    MKASTNodeId condition = parser_parse_expression(parser);
    if (condition == MK_AST_NODE_NIL ||
        !parser_expect(parser, MK_TOK_RPAREN, "右圆括号"))
        goto Failed;
    frame->kind = MK_TOP_FRAME_IF;
    frame->stage = MK_TOP_STAGE_THEN;
    frame->start = start;
    frame->condition = condition;
    return CCTRUE;

Failed:
    parser_leave_syntax(parser);
    return CCFALSE;
}

static CCBOOL parser_top_begin_block(
    MKParser *parser, MKTopFrame *frame
)
{
    memset(frame, 0, sizeof(*frame));
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
    MKASTNode *block_node = mk_ast_node_mut(parser->ast, block);
    if (!block_node)
    {
        parser_leave_syntax(parser);
        return CCFALSE;
    }
    block_node->as.block.is_top_level = CCTRUE;
    frame->kind = MK_TOP_FRAME_BLOCK;
    frame->stage = MK_TOP_STAGE_ITEMS;
    frame->start = start;
    frame->node = block;
    return CCTRUE;
}

static CCBOOL parser_top_push_if(
    MKParser *parser, CCDyn *frames
)
{
    MKTopFrame frame;
    if (!parser_top_begin_if(parser, &frame))
        return CCFALSE;
    if (!parser_top_frame_push(parser, frames, &frame))
    {
        parser_leave_syntax(parser);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL parser_top_push_block(
    MKParser *parser, CCDyn *frames
)
{
    MKTopFrame frame;
    if (!parser_top_begin_block(parser, &frame))
        return CCFALSE;
    if (!parser_top_frame_push(parser, frames, &frame))
    {
        parser_leave_syntax(parser);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL parser_top_finalize(
    MKParser *parser, MKTopFrame *frame, MKASTNodeId *completed
)
{
    if (frame->kind == MK_TOP_FRAME_BLOCK)
    {
        if (!parser_expect(parser, MK_TOK_RBRACE, "右花括号"))
            return CCFALSE;
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        if (!parser_set_node_span(parser, frame->node, &span))
            return CCFALSE;
        *completed = frame->node;
        parser_leave_syntax(parser);
        return CCTRUE;
    }

    MKSourceSpan span =
        parser_make_span(&frame->start, &parser->previous);
    MKASTNodeId node = parser_new_node(parser, MK_AST_IF, &span);
    if (node == MK_AST_NODE_NIL)
        return CCFALSE;
    MKASTNode *if_node = mk_ast_node_mut(parser->ast, node);
    if (!if_node)
        return CCFALSE;
    if_node->as.if_stmt.has_else = frame->has_else;
    if_node->as.if_stmt.is_top_level = CCTRUE;
    if (!parser_append_child(parser, node, frame->condition) ||
        !parser_append_child(parser, node, frame->first) ||
        (frame->has_else &&
         !parser_append_child(parser, node, frame->second)))
        return CCFALSE;
    *completed = node;
    parser_leave_syntax(parser);
    return CCTRUE;
}

static MKASTNodeId parser_parse_top_tree(MKParser *parser)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    MKASTNodeId completed = MK_AST_NODE_NIL;
    CCBOOL has_completed = CCFALSE;

    for (;;)
    {
        if (has_completed)
        {
            MKTopFrame *frame = parser_top_frame_top(&frames);
            if (!frame)
            {
                ccalloc(frames.array, 0, NULL);
                return completed;
            }

            if (frame->kind == MK_TOP_FRAME_BLOCK)
            {
                if (!parser_append_child(parser, frame->node, completed))
                    goto Failed;
                completed = MK_AST_NODE_NIL;
                has_completed = CCFALSE;
                continue;
            }

            if (frame->stage == MK_TOP_STAGE_THEN)
            {
                frame->first = completed;
                completed = MK_AST_NODE_NIL;
                if (parser->current.type == MK_TOK_KW_ELSE)
                {
                    if (!parser_advance(parser))
                        goto Failed;
                    frame->stage = MK_TOP_STAGE_ELSE;
                    has_completed = CCFALSE;
                    continue;
                }
            }
            else
            {
                frame->second = completed;
                frame->has_else = CCTRUE;
                completed = MK_AST_NODE_NIL;
            }

            MKTopFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!parser_top_finalize(parser, frame, &completed) ||
                !parser_top_frame_pop(&frames, &popped))
                goto Failed;
            has_completed = CCTRUE;
            continue;
        }

        if (frames.size == 0)
        {
            if (parser->current.type == MK_TOK_KW_IF)
            {
                if (!parser_top_push_if(parser, &frames))
                    goto Failed;
                continue;
            }
            if (parser->current.type == MK_TOK_LBRACE)
            {
                if (!parser_top_push_block(parser, &frames))
                    goto Failed;
                continue;
            }
            (void)parser_error_current(
                parser, "顶层只允许 include/import/变量/函数声明"
            );
            goto Failed;
        }

        MKTopFrame *frame = parser_top_frame_top(&frames);
        if (!frame)
            goto Failed;

        if (frame->stage == MK_TOP_STAGE_ITEMS)
        {
            if (parser->current.type == MK_TOK_RBRACE)
            {
                if (!parser_top_finalize(parser, frame, &completed))
                    goto Failed;
                MKTopFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!parser_top_frame_pop(&frames, &popped))
                    goto Failed;
                has_completed = CCTRUE;
                continue;
            }
            if (parser->current.type == MK_TOK_EOF)
            {
                (void)parser_error_current(parser, "顶层 if 块未闭合");
                goto Failed;
            }
            if (parser->current.type == MK_TOK_KW_IF)
            {
                if (!parser_top_push_if(parser, &frames))
                    goto Failed;
                continue;
            }
            if (parser->current.type == MK_TOK_LBRACE)
            {
                if (!parser_top_push_block(parser, &frames))
                    goto Failed;
                continue;
            }
            (void)parser_error_current(
                parser, "顶层只允许 include/import/变量/函数声明"
            );
            goto Failed;
        }

        if (parser->current.type == MK_TOK_KW_IF)
        {
            if (!parser_top_push_if(parser, &frames))
                goto Failed;
            continue;
        }
        if (parser->current.type == MK_TOK_LBRACE)
        {
            if (!parser_top_push_block(parser, &frames))
                goto Failed;
            continue;
        }
        (void)parser_error_current(
            parser, "顶层只允许 include/import/变量/函数声明"
        );
        goto Failed;
    }

Failed:
    {
        CCUINT32 count = frames.size / (CCUINT32)sizeof(MKTopFrame);
        for (CCUINT32 i = 0; i < count; i++)
            parser_leave_syntax(parser);
        if (frames.array)
            ccalloc(frames.array, 0, NULL);
    }
    return MK_AST_NODE_NIL;
}

static MKASTNodeId parser_parse_top_if(MKParser *parser)
{
    return parser_parse_top_tree(parser);
}


/* ============================================================================
 * 模块
 * ========================================================================== */

static MKASTNodeId parser_parse_top_item(MKParser *parser)
{
    switch (parser->current.type)
    {
    case MK_TOK_AT_INCLUDE:
        return parser_parse_include(parser);

    case MK_TOK_AT_IMPORT:
        return parser_parse_import(parser);

    case MK_TOK_KW_FUNC:
        return parser_parse_func_decl(parser);


    case MK_TOK_KW_IF:
        return parser_parse_top_if(parser);

    default:
        if (parser->current.type == MK_TOK_KW_CONST ||
            parser_is_type_start(parser))
            return parser_parse_var_decl(parser);
        (void)parser_error_current(
            parser, "期望顶层声明，实际%s",
            mk_token_type_name(parser->current.type)
        );
        return MK_AST_NODE_NIL;
    }
}

CCBOOL parser_parse_module(MKParser *parser)
{
    if (!parser || !parser->ast)
        return CCFALSE;
    if (!parser_enter_syntax(parser))
        return CCFALSE;

    MKToken start = parser->current;
    MKASTNodeId module = mk_ast_root(parser->ast);

    while (parser->current.type != MK_TOK_EOF)
    {
        if (!parser_count_declaration(parser))
            goto Failed;

        MKASTNodeId item = parser_parse_top_item(parser);
        if (item == MK_AST_NODE_NIL)
            goto Failed;
        if (!parser_append_child(parser, module, item))
            goto Failed;
    }

    {
        const MKToken *end =
            parser->previous.line != 0
                ? &parser->previous
                : &parser->current;
        MKSourceSpan span = parser_make_span(&start, end);
        if (!parser_set_node_span(parser, module, &span))
            goto Failed;
    }

    parser_leave_syntax(parser);
    return CCTRUE;

Failed:
    parser_leave_syntax(parser);
    return CCFALSE;
}
