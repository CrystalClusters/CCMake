/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 声明 Parser：模块、导入、函数、target、target 字段与顶层 if
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_parser_internal.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <string.h>

static CMKASTNodeId parser_parse_include(CMKParser *parser);
static CMKASTNodeId parser_parse_import(CMKParser *parser);
static CMKASTNodeId parser_parse_func_decl(CMKParser *parser);
static CMKASTNodeId parser_parse_target_decl(CMKParser *parser);
static CMKASTNodeId parser_parse_top_if(CMKParser *parser);

/* ============================================================================
 * 类型
 * ========================================================================== */

CCBOOL parser_is_type_start(CMKParser *parser)
{
    if (!parser)
        return CCFALSE;
    switch (parser->current.type)
    {
    case CMK_TOK_KW_STRING:
    case CMK_TOK_KW_INT:
    case CMK_TOK_KW_BOOL:
    case CMK_TOK_KW_FLOAT:
    case CMK_TOK_KW_ACTION:
    case CMK_TOK_KW_TARGET_REF:
    case CMK_TOK_KW_LIST:
        return CCTRUE;

    default:
        return CCFALSE;
    }
}

static CMKASTNodeId parser_make_scalar_type(
    CMKParser *parser, CMKTypeKind kind
)
{
    CMKToken token = parser->current;
    CMKSourceSpan span = parser_token_span(&token);
    CMKASTNodeId node = parser_new_node(parser, CMK_AST_TYPE, &span);
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CMKASTNode *type_node = cmk_ast_node_mut(parser->ast, node);
    if (!type_node)
        return CMK_AST_NODE_NIL;
    type_node->as.type.kind = kind;

    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;
    return node;
}

CMKASTNodeId parser_parse_type(CMKParser *parser)
{
    if (!parser || parser->failed)
        return CMK_AST_NODE_NIL;

    CCDyn layers;
    memset(&layers, 0, sizeof(layers));
    CCUINT32 entered = 0;
    CMKASTNodeId base = CMK_AST_NODE_NIL;
    CCUINT32 layer_count = 0;

    while (parser->current.type == CMK_TOK_KW_LIST)
    {
        if (!parser_enter_syntax(parser))
            goto Failed;
        entered++;
        CMKToken start = parser->current;
        if (!cmk_dyn_push(
                &layers, (CCUINT32)sizeof(CMKToken),
                (CCINT32)sizeof(CMKToken), &start
            ))
        {
            (void)parser_error_current(parser, "内存分配失败");
            goto Failed;
        }
        layer_count++;
        if (!parser_advance(parser))
            goto Failed;
        if (!parser_expect(parser, CMK_TOK_LT, "`<`"))
            goto Failed;
    }

    if (!parser_enter_syntax(parser))
        goto Failed;
    entered++;

    switch (parser->current.type)
    {
    case CMK_TOK_KW_STRING:
        base = parser_make_scalar_type(parser, CMK_TYPE_STRING);
        break;

    case CMK_TOK_KW_INT:
        base = parser_make_scalar_type(parser, CMK_TYPE_INT);
        break;

    case CMK_TOK_KW_BOOL:
        base = parser_make_scalar_type(parser, CMK_TYPE_BOOL);
        break;

    case CMK_TOK_KW_FLOAT:
        base = parser_make_scalar_type(parser, CMK_TYPE_FLOAT);
        break;

    case CMK_TOK_KW_ACTION:
        base = parser_make_scalar_type(parser, CMK_TYPE_ACTION);
        break;

    case CMK_TOK_KW_TARGET_REF:
        base = parser_make_scalar_type(parser, CMK_TYPE_TARGET_REF);
        break;

    default:
        (void)parser_error_current(
            parser, "期望类型，实际%s",
            cmk_token_type_name(parser->current.type)
        );
        break;
    }
    parser_leave_syntax(parser);
    entered--;
    if (base == CMK_AST_NODE_NIL)
        goto Failed;

    for (CCUINT32 i = layer_count; i > 0; i--)
    {
        if (!parser_expect(parser, CMK_TOK_GT, "`>`"))
            goto Failed;

        CMKToken *starts = (CMKToken *)layers.array;
        CMKSourceSpan span =
            parser_make_span(&starts[i - 1], &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_TYPE, &span
        );
        if (node == CMK_AST_NODE_NIL)
            goto Failed;
        CMKASTNode *type_node = cmk_ast_node_mut(parser->ast, node);
        if (!type_node)
            goto Failed;
        type_node->as.type.kind = CMK_TYPE_LIST;
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
    return CMK_AST_NODE_NIL;
}

CMKASTNodeId parser_parse_return_type(CMKParser *parser)
{
    if (!parser || parser->failed)
        return CMK_AST_NODE_NIL;
    if (parser->current.type == CMK_TOK_KW_VOID)
        return parser_make_scalar_type(parser, CMK_TYPE_VOID);
    return parser_parse_type(parser);
}

/* ============================================================================
 * 变量声明
 * ========================================================================== */

CMKASTNodeId parser_parse_var_decl(CMKParser *parser)
{
    CMKToken start = parser->current;
    CCBOOL is_const = CCFALSE;
    if (parser->current.type == CMK_TOK_KW_CONST)
    {
        is_const = CCTRUE;
        if (!parser_advance(parser))
            return CMK_AST_NODE_NIL;
    }

    CMKASTNodeId type = parser_parse_type(parser);
    if (type == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CMKASTStringId name = CMK_AST_STRING_NIL;
    if (!parser_take_identifier(parser, &name))
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_ASSIGN, "`=`"))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId value = parser_parse_expression(parser);
    if (value == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    CMKASTNodeId node = parser_new_node(
        parser, CMK_AST_VAR_DECL, &span
    );
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *var_node = cmk_ast_node_mut(parser->ast, node);
    if (!var_node)
        return CMK_AST_NODE_NIL;
    var_node->as.var_decl.name = name;
    var_node->as.var_decl.is_const = is_const;
    if (!parser_append_child(parser, node, type) ||
        !parser_append_child(parser, node, value))
        return CMK_AST_NODE_NIL;
    return node;
}

/* ============================================================================
 * include / import
 * ========================================================================== */

static CMKASTNodeId parser_parse_include(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, CMK_TOK_AT_INCLUDE, "`@include`"))
        return CMK_AST_NODE_NIL;

    CMKASTStringId module = CMK_AST_STRING_NIL;
    if (!parser_take_string(parser, &module))
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    CMKASTNodeId node = parser_new_node(
        parser, CMK_AST_INCLUDE_DECL, &span
    );
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *include_node = cmk_ast_node_mut(parser->ast, node);
    if (!include_node)
        return CMK_AST_NODE_NIL;
    include_node->as.include_decl.module = module;
    return node;
}

static CMKASTNodeId parser_make_import_item(
    CMKParser *parser, CMKToken name_token, CMKASTStringId name
)
{
    CMKSourceSpan span = parser_token_span(&name_token);
    CMKASTNodeId node = parser_new_node(
        parser, CMK_AST_IMPORT_ITEM, &span
    );
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *item = cmk_ast_node_mut(parser->ast, node);
    if (!item)
        return CMK_AST_NODE_NIL;
    item->as.import_item.name = name;
    item->as.import_item.alias = CMK_AST_STRING_NIL;
    return node;
}

static CMKASTNodeId parser_parse_import(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, CMK_TOK_AT_IMPORT, "`@import`"))
        return CMK_AST_NODE_NIL;

    CCBOOL is_func = CCFALSE;
    if (parser->current.type == CMK_TOK_KW_FUNC)
        is_func = CCTRUE;
    else if (parser->current.type != CMK_TOK_KW_TARGET)
        return parser_error_current(
            parser, "导入需要 `func` 或 `target`"
        ), CMK_AST_NODE_NIL;

    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan zero_span;
    memset(&zero_span, 0, sizeof(zero_span));
    CMKASTNodeId node = parser_new_node(
        parser, CMK_AST_IMPORT_DECL, &zero_span
    );
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *import_node = cmk_ast_node_mut(parser->ast, node);
    if (!import_node)
        return CMK_AST_NODE_NIL;
    import_node->as.import_decl.kind =
        is_func ? CMK_IMPORT_FUNC : CMK_IMPORT_TARGET;
    import_node->as.import_decl.module = CMK_AST_STRING_NIL;
    import_node->as.import_decl.alias = CMK_AST_STRING_NIL;
    import_node->as.import_decl.wildcard = CCFALSE;

    CCUINT32 item_count = 0;
    CMKToken first_item_start;
    memset(&first_item_start, 0, sizeof(first_item_start));
    if (is_func && parser->current.type == CMK_TOK_STAR)
    {
        import_node = cmk_ast_node_mut(parser->ast, node);
        if (!import_node)
            return CMK_AST_NODE_NIL;
        import_node->as.import_decl.wildcard = CCTRUE;
        if (!parser_advance(parser))
            return CMK_AST_NODE_NIL;
    }
    else
    {
        for (;;)
        {
            CMKToken name_token = parser->current;
            CMKASTStringId name = CMK_AST_STRING_NIL;
            if (!parser_take_identifier(parser, &name))
                return CMK_AST_NODE_NIL;

            CMKASTNodeId item = parser_make_import_item(
                parser, name_token, name
            );
            if (item == CMK_AST_NODE_NIL)
                return CMK_AST_NODE_NIL;
            if (!parser_append_child(parser, node, item))
                return CMK_AST_NODE_NIL;
            if (item_count == 0)
                first_item_start = name_token;
            item_count++;

            if (parser->current.type != CMK_TOK_COMMA)
                break;
            if (!parser_advance(parser))
                return CMK_AST_NODE_NIL;
        }
    }

    if (is_func)
    {
        if (parser->current.type == CMK_TOK_KW_AS)
        {
            if (item_count != 1)
                return parser_error_current(
                    parser, "函数批量导入不能使用 `as`"
                ), CMK_AST_NODE_NIL;
            if (!parser_advance(parser))
                return CMK_AST_NODE_NIL;

            CMKASTStringId alias = CMK_AST_STRING_NIL;
            if (!parser_take_identifier(parser, &alias))
                return CMK_AST_NODE_NIL;

            import_node = cmk_ast_node_mut(parser->ast, node);
            if (!import_node)
                return CMK_AST_NODE_NIL;
            CMKASTNode *item_node = cmk_ast_node_mut(
                parser->ast, import_node->first_child
            );
            if (!item_node)
                return CMK_AST_NODE_NIL;
            item_node->as.import_item.alias = alias;

            CMKSourceSpan item_span = parser_make_span(
                &first_item_start, &parser->previous
            );
            item_node->span = item_span;
        }
    }

    if (!parser_expect(parser, CMK_TOK_KW_FROM, "`from`"))
        return CMK_AST_NODE_NIL;

    CMKASTStringId module = CMK_AST_STRING_NIL;
    if (!parser_take_string(parser, &module))
        return CMK_AST_NODE_NIL;

    if (!is_func)
    {
        if (!parser_expect(parser, CMK_TOK_KW_AS, "`as`"))
            return CMK_AST_NODE_NIL;
        CMKASTStringId alias = CMK_AST_STRING_NIL;
        if (!parser_take_identifier(parser, &alias))
            return CMK_AST_NODE_NIL;
        import_node = cmk_ast_node_mut(parser->ast, node);
        if (!import_node)
            return CMK_AST_NODE_NIL;
        import_node->as.import_decl.alias = alias;
    }

    if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
        return CMK_AST_NODE_NIL;

    import_node = cmk_ast_node_mut(parser->ast, node);
    if (!import_node)
        return CMK_AST_NODE_NIL;
    import_node->as.import_decl.module = module;
    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    if (!parser_set_node_span(parser, node, &span))
        return CMK_AST_NODE_NIL;
    return node;
}

/* ============================================================================
 * 函数声明
 * ========================================================================== */

static CMKASTNodeId parser_parse_func_decl(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, CMK_TOK_KW_FUNC, "`func`"))
        return CMK_AST_NODE_NIL;

    CMKASTStringId name = CMK_AST_STRING_NIL;
    if (!parser_take_identifier(parser, &name))
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
        return CMK_AST_NODE_NIL;

    CCDyn params;
    memset(&params, 0, sizeof(params));
    CMKASTNodeId func_id = CMK_AST_NODE_NIL;

    if (parser->current.type != CMK_TOK_RPAREN)
    {
        for (;;)
        {
            CCUINT32 count =
                params.size / (CCUINT32)sizeof(CMKASTNodeId);
            if (parser->limits.max_function_params != 0 &&
                count >= parser->limits.max_function_params)
            {
                (void)parser_error_current(
                    parser, "函数参数数量超过限制"
                );
                goto Failed;
            }

            CMKToken param_start = parser->current;
            CMKASTNodeId type = parser_parse_type(parser);
            if (type == CMK_AST_NODE_NIL)
                goto Failed;

            CMKASTStringId param_name = CMK_AST_STRING_NIL;
            if (!parser_take_identifier(parser, &param_name))
                goto Failed;

            CMKSourceSpan span =
                parser_make_span(&param_start, &parser->previous);
            CMKASTNodeId param = parser_new_node(
                parser, CMK_AST_PARAM, &span
            );
            if (param == CMK_AST_NODE_NIL)
                goto Failed;
            CMKASTNode *param_node = cmk_ast_node_mut(parser->ast, param);
            if (!param_node)
                goto Failed;
            param_node->as.param.name = param_name;
            if (!parser_append_child(parser, param, type))
                goto Failed;
            if (!cmk_dyn_push(
                    &params,
                    (CCUINT32)sizeof(CMKASTNodeId),
                    (CCINT32)sizeof(CMKASTNodeId),
                    &param
                ))
            {
                (void)parser_error_current(parser, "内存分配失败");
                goto Failed;
            }

            if (parser->current.type != CMK_TOK_COMMA)
                break;
            if (!parser_advance(parser))
                goto Failed;
            if (parser->current.type == CMK_TOK_RPAREN)
            {
                (void)parser_error_current(
                    parser, "函数参数不允许尾逗号"
                );
                goto Failed;
            }
        }
    }

    if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
        goto Failed;
    if (!parser_expect(parser, CMK_TOK_ARROW, "`->`"))
        goto Failed;

    CMKASTNodeId return_type = parser_parse_return_type(parser);
    if (return_type == CMK_AST_NODE_NIL)
        goto Failed;
    CMKASTNodeId body = parser_parse_block(parser);
    if (body == CMK_AST_NODE_NIL)
        goto Failed;

    {
        CMKSourceSpan span =
            parser_make_span(&start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_FUNC_DECL, &span
        );
        if (node == CMK_AST_NODE_NIL)
            goto Failed;
        CMKASTNode *func_node = cmk_ast_node_mut(parser->ast, node);
        if (!func_node)
            goto Failed;
        func_node->as.func_decl.name = name;
        func_node->as.func_decl.param_count =
            params.size / (CCUINT32)sizeof(CMKASTNodeId);
        if (!parser_append_child(parser, node, return_type))
            goto Failed;
        CCUINT32 param_count =
            params.size / (CCUINT32)sizeof(CMKASTNodeId);
        for (CCUINT32 i = 0; i < param_count; i++)
        {
            CMKASTNodeId param = CMK_AST_NODE_NIL;
            if (cc_dyn_core_seek(
                    &params,
                    (CCUINT32)sizeof(CMKASTNodeId),
                    (CCINT32)sizeof(CMKASTNodeId),
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
    return CMK_AST_NODE_NIL;
}

/* ============================================================================
 * target
 * ========================================================================== */

static CMKTargetFieldKind parser_target_field_kind(
    const char *text, CCUINT32 len
)
{
    if (len == 10 && memcmp(text, "depends_on", 10) == 0)
        return CMK_TARGET_FIELD_DEPENDS_ON;
    if (len == 6 && memcmp(text, "inputs", 6) == 0)
        return CMK_TARGET_FIELD_INPUTS;
    if (len == 7 && memcmp(text, "outputs", 7) == 0)
        return CMK_TARGET_FIELD_OUTPUTS;
    if (len == 7 && memcmp(text, "actions", 7) == 0)
        return CMK_TARGET_FIELD_ACTIONS;
    return CMK_TARGET_FIELD_UNKNOWN;
}

CCBOOL parser_parse_target_fields(
    CMKParser *parser, CMKASTNodeId target_id
)
{
    if (!parser_expect(parser, CMK_TOK_LBRACE, "左花括号"))
        return CCFALSE;
    if (!parser_enter_syntax(parser))
        return CCFALSE;

    while (parser->current.type != CMK_TOK_RBRACE)
    {
        if (parser->current.type == CMK_TOK_EOF)
        {
            (void)parser_error_current(parser, "target 体未闭合");
            goto Failed;
        }

        CMKToken field_start = parser->current;
        CMKASTNodeId type = parser_parse_type(parser);
        if (type == CMK_AST_NODE_NIL)
            goto Failed;

        CMKToken name_token = parser->current;
        CMKASTStringId name = CMK_AST_STRING_NIL;
        if (!parser_take_identifier(parser, &name))
            goto Failed;
        if (!parser_expect(parser, CMK_TOK_ASSIGN, "`=`"))
            goto Failed;

        CMKASTNodeId value = parser_parse_expression(parser);
        if (value == CMK_AST_NODE_NIL)
            goto Failed;
        if (!parser_expect(parser, CMK_TOK_SEMI, "分号"))
            goto Failed;

        CMKSourceSpan span =
            parser_make_span(&field_start, &parser->previous);
        CMKASTNodeId field = parser_new_node(
            parser, CMK_AST_TARGET_FIELD, &span
        );
        if (field == CMK_AST_NODE_NIL)
            goto Failed;
        CMKASTNode *field_node = cmk_ast_node_mut(parser->ast, field);
        if (!field_node)
            goto Failed;
        field_node->as.target_field.field = parser_target_field_kind(
            name_token.text, name_token.len
        );
        field_node->as.target_field.name = name;
        if (!parser_append_child(parser, field, type) ||
            !parser_append_child(parser, field, value) ||
            !parser_append_child(parser, target_id, field))
            goto Failed;
    }

    if (!parser_expect(parser, CMK_TOK_RBRACE, "右花括号"))
        goto Failed;

    parser_leave_syntax(parser);
    return CCTRUE;

Failed:
    parser_leave_syntax(parser);
    return CCFALSE;
}

static CMKASTNodeId parser_parse_target_decl(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, CMK_TOK_KW_TARGET, "`target`"))
        return CMK_AST_NODE_NIL;

    CMKASTStringId name = CMK_AST_STRING_NIL;
    if (!parser_take_identifier(parser, &name))
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
        return CMK_AST_NODE_NIL;
    if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan zero_span;
    memset(&zero_span, 0, sizeof(zero_span));
    CMKASTNodeId node = parser_new_node(
        parser, CMK_AST_TARGET_DECL, &zero_span
    );
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *target_node = cmk_ast_node_mut(parser->ast, node);
    if (!target_node)
        return CMK_AST_NODE_NIL;
    target_node->as.target_decl.name = name;

    if (!parser_enter_syntax(parser))
        return CMK_AST_NODE_NIL;
    if (!parser_parse_target_fields(parser, node))
    {
        parser_leave_syntax(parser);
        return CMK_AST_NODE_NIL;
    }
    parser_leave_syntax(parser);

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    if (!parser_set_node_span(parser, node, &span))
        return CMK_AST_NODE_NIL;
    return node;
}

/* ============================================================================
 * 顶层 if
 * ========================================================================== */

/* ============================================================================
 * 顶层 if：显式分支帧栈
 * ========================================================================== */

typedef enum cmk_top_frame_kind {
    CMK_TOP_FRAME_IF = 0,
    CMK_TOP_FRAME_BLOCK
} CMKTopFrameKind;

typedef enum cmk_top_frame_stage {
    CMK_TOP_STAGE_THEN = 0,
    CMK_TOP_STAGE_ELSE,
    CMK_TOP_STAGE_ITEMS
} CMKTopFrameStage;

typedef struct cmk_top_frame {
    CMKTopFrameKind kind;
    CMKTopFrameStage stage;
    CMKToken start;
    CMKASTNodeId node;
    CMKASTNodeId condition;
    CMKASTNodeId first;
    CMKASTNodeId second;
    CCBOOL has_else;
} CMKTopFrame;

static CMKTopFrame *parser_top_frame_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(CMKTopFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKTopFrame);
    return &((CMKTopFrame *)frames->array)[count - 1];
}

static CCBOOL parser_top_frame_push(
    CMKParser *parser, CCDyn *frames, const CMKTopFrame *frame
)
{
    if (!cmk_dyn_push(
            frames, (CCUINT32)sizeof(CMKTopFrame),
            (CCINT32)sizeof(CMKTopFrame), frame
        ))
        return parser_error_current(parser, "内存分配失败"), CCFALSE;
    return CCTRUE;
}

static CCBOOL parser_top_frame_pop(CCDyn *frames, CMKTopFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(CMKTopFrame),
        (CCINT32)sizeof(CMKTopFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static CCBOOL parser_top_begin_if(
    CMKParser *parser, CMKTopFrame *frame
)
{
    memset(frame, 0, sizeof(*frame));
    CMKToken start = parser->current;
    if (!parser_enter_syntax(parser))
        return CCFALSE;
    if (!parser_expect(parser, CMK_TOK_KW_IF, "`if`") ||
        !parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
        goto Failed;
    CMKASTNodeId condition = parser_parse_expression(parser);
    if (condition == CMK_AST_NODE_NIL ||
        !parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
        goto Failed;
    frame->kind = CMK_TOP_FRAME_IF;
    frame->stage = CMK_TOP_STAGE_THEN;
    frame->start = start;
    frame->condition = condition;
    return CCTRUE;

Failed:
    parser_leave_syntax(parser);
    return CCFALSE;
}

static CCBOOL parser_top_begin_block(
    CMKParser *parser, CMKTopFrame *frame
)
{
    memset(frame, 0, sizeof(*frame));
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
    CMKASTNode *block_node = cmk_ast_node_mut(parser->ast, block);
    if (!block_node)
    {
        parser_leave_syntax(parser);
        return CCFALSE;
    }
    block_node->as.block.is_top_level = CCTRUE;
    frame->kind = CMK_TOP_FRAME_BLOCK;
    frame->stage = CMK_TOP_STAGE_ITEMS;
    frame->start = start;
    frame->node = block;
    return CCTRUE;
}

static CCBOOL parser_top_push_if(
    CMKParser *parser, CCDyn *frames
)
{
    CMKTopFrame frame;
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
    CMKParser *parser, CCDyn *frames
)
{
    CMKTopFrame frame;
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
    CMKParser *parser, CMKTopFrame *frame, CMKASTNodeId *completed
)
{
    if (frame->kind == CMK_TOP_FRAME_BLOCK)
    {
        if (!parser_expect(parser, CMK_TOK_RBRACE, "右花括号"))
            return CCFALSE;
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        if (!parser_set_node_span(parser, frame->node, &span))
            return CCFALSE;
        *completed = frame->node;
        parser_leave_syntax(parser);
        return CCTRUE;
    }

    CMKSourceSpan span =
        parser_make_span(&frame->start, &parser->previous);
    CMKASTNodeId node = parser_new_node(parser, CMK_AST_IF, &span);
    if (node == CMK_AST_NODE_NIL)
        return CCFALSE;
    CMKASTNode *if_node = cmk_ast_node_mut(parser->ast, node);
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

static CMKASTNodeId parser_parse_top_tree(CMKParser *parser)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CMKASTNodeId completed = CMK_AST_NODE_NIL;
    CCBOOL has_completed = CCFALSE;

    for (;;)
    {
        if (has_completed)
        {
            CMKTopFrame *frame = parser_top_frame_top(&frames);
            if (!frame)
            {
                ccalloc(frames.array, 0, NULL);
                return completed;
            }

            if (frame->kind == CMK_TOP_FRAME_BLOCK)
            {
                if (!parser_append_child(parser, frame->node, completed))
                    goto Failed;
                completed = CMK_AST_NODE_NIL;
                has_completed = CCFALSE;
                continue;
            }

            if (frame->stage == CMK_TOP_STAGE_THEN)
            {
                frame->first = completed;
                completed = CMK_AST_NODE_NIL;
                if (parser->current.type == CMK_TOK_KW_ELSE)
                {
                    if (!parser_advance(parser))
                        goto Failed;
                    frame->stage = CMK_TOP_STAGE_ELSE;
                    has_completed = CCFALSE;
                    continue;
                }
            }
            else
            {
                frame->second = completed;
                frame->has_else = CCTRUE;
                completed = CMK_AST_NODE_NIL;
            }

            CMKTopFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!parser_top_finalize(parser, frame, &completed) ||
                !parser_top_frame_pop(&frames, &popped))
                goto Failed;
            has_completed = CCTRUE;
            continue;
        }

        if (frames.size == 0)
        {
            if (parser->current.type == CMK_TOK_KW_TARGET)
                return parser_parse_target_decl(parser);
            if (parser->current.type == CMK_TOK_KW_IF)
            {
                if (!parser_top_push_if(parser, &frames))
                    goto Failed;
                continue;
            }
            if (parser->current.type == CMK_TOK_LBRACE)
            {
                if (!parser_top_push_block(parser, &frames))
                    goto Failed;
                continue;
            }
            (void)parser_error_current(
                parser, "顶层 if 分支只允许 target 声明"
            );
            goto Failed;
        }

        CMKTopFrame *frame = parser_top_frame_top(&frames);
        if (!frame)
            goto Failed;

        if (frame->stage == CMK_TOP_STAGE_ITEMS)
        {
            if (parser->current.type == CMK_TOK_RBRACE)
            {
                if (!parser_top_finalize(parser, frame, &completed))
                    goto Failed;
                CMKTopFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!parser_top_frame_pop(&frames, &popped))
                    goto Failed;
                has_completed = CCTRUE;
                continue;
            }
            if (parser->current.type == CMK_TOK_KW_TARGET)
            {
                CMKASTNodeId item = parser_parse_target_decl(parser);
                if (item == CMK_AST_NODE_NIL ||
                    !parser_append_child(parser, frame->node, item))
                    goto Failed;
                continue;
            }
            if (parser->current.type == CMK_TOK_EOF)
            {
                (void)parser_error_current(parser, "顶层 if 块未闭合");
                goto Failed;
            }
            if (parser->current.type == CMK_TOK_KW_IF)
            {
                if (!parser_top_push_if(parser, &frames))
                    goto Failed;
                continue;
            }
            if (parser->current.type == CMK_TOK_LBRACE)
            {
                if (!parser_top_push_block(parser, &frames))
                    goto Failed;
                continue;
            }
            (void)parser_error_current(
                parser, "顶层 if 分支只允许 target 声明"
            );
            goto Failed;
        }

        if (parser->current.type == CMK_TOK_KW_TARGET)
        {
            completed = parser_parse_target_decl(parser);
            if (completed == CMK_AST_NODE_NIL)
                goto Failed;
            has_completed = CCTRUE;
            continue;
        }
        if (parser->current.type == CMK_TOK_KW_IF)
        {
            if (!parser_top_push_if(parser, &frames))
                goto Failed;
            continue;
        }
        if (parser->current.type == CMK_TOK_LBRACE)
        {
            if (!parser_top_push_block(parser, &frames))
                goto Failed;
            continue;
        }
        (void)parser_error_current(
            parser, "顶层 if 分支只允许 target 声明"
        );
        goto Failed;
    }

Failed:
    {
        CCUINT32 count = frames.size / (CCUINT32)sizeof(CMKTopFrame);
        for (CCUINT32 i = 0; i < count; i++)
            parser_leave_syntax(parser);
        if (frames.array)
            ccalloc(frames.array, 0, NULL);
    }
    return CMK_AST_NODE_NIL;
}

static CMKASTNodeId parser_parse_top_if(CMKParser *parser)
{
    return parser_parse_top_tree(parser);
}


/* ============================================================================
 * 模块
 * ========================================================================== */

static CMKASTNodeId parser_parse_top_item(CMKParser *parser)
{
    switch (parser->current.type)
    {
    case CMK_TOK_AT_INCLUDE:
        return parser_parse_include(parser);

    case CMK_TOK_AT_IMPORT:
        return parser_parse_import(parser);

    case CMK_TOK_KW_FUNC:
        return parser_parse_func_decl(parser);

    case CMK_TOK_KW_TARGET:
        return parser_parse_target_decl(parser);

    case CMK_TOK_KW_IF:
        return parser_parse_top_if(parser);

    default:
        if (parser->current.type == CMK_TOK_KW_CONST ||
            parser_is_type_start(parser))
            return parser_parse_var_decl(parser);
        (void)parser_error_current(
            parser, "期望顶层声明，实际%s",
            cmk_token_type_name(parser->current.type)
        );
        return CMK_AST_NODE_NIL;
    }
}

CCBOOL parser_parse_module(CMKParser *parser)
{
    if (!parser || !parser->ast)
        return CCFALSE;
    if (!parser_enter_syntax(parser))
        return CCFALSE;

    CMKToken start = parser->current;
    CMKASTNodeId module = cmk_ast_root(parser->ast);

    while (parser->current.type != CMK_TOK_EOF)
    {
        if (!parser_count_declaration(parser))
            goto Failed;

        CMKASTNodeId item = parser_parse_top_item(parser);
        if (item == CMK_AST_NODE_NIL)
            goto Failed;
        if (!parser_append_child(parser, module, item))
            goto Failed;
    }

    {
        const CMKToken *end =
            parser->previous.line != 0
                ? &parser->previous
                : &parser->current;
        CMKSourceSpan span = parser_make_span(&start, end);
        if (!parser_set_node_span(parser, module, &span))
            goto Failed;
    }

    parser_leave_syntax(parser);
    return CCTRUE;

Failed:
    parser_leave_syntax(parser);
    return CCFALSE;
}
