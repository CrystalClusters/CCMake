/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 表达式 Parser：Pratt、primary、postfix、list 与字面量
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_parser_internal.h"
#include "cmk_safe.h"

#include <ccalloc.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * 字符与配置路径
 * ========================================================================== */

static CCBOOL parser_is_digit(char c)
{
    return (c >= '0' && c <= '9') ? CCTRUE : CCFALSE;
}

static CCBOOL parser_is_ident_start(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
        return CCTRUE;
    return CCFALSE;
}

static CCBOOL parser_is_ident_continue(char c)
{
    return (parser_is_ident_start(c) || parser_is_digit(c))
        ? CCTRUE : CCFALSE;
}

static CCBOOL parser_config_path_ok(const char *text, CCUINT32 len)
{
    if (!text || len == 0)
        return CCFALSE;

    CCUINT32 i = 0;
    if (!parser_is_ident_start(text[i]))
        return CCFALSE;
    while (i < len && parser_is_ident_continue(text[i]))
        i++;

    while (i < len)
    {
        if (text[i] == '.')
        {
            i++;
            if (i >= len || !parser_is_ident_start(text[i]))
                return CCFALSE;
            while (i < len && parser_is_ident_continue(text[i]))
                i++;
        }
        else if (text[i] == '[')
        {
            i++;
            CCUINT32 digits = i;
            while (i < len && parser_is_digit(text[i]))
                i++;
            if (i == digits || i >= len || text[i] != ']')
                return CCFALSE;
            i++;
        }
        else
            return CCFALSE;
    }
    return CCTRUE;
}

/* ============================================================================
 * 字面量转换
 * ========================================================================== */

static CCBOOL parser_int_magnitude(
    CMKParser *parser, const CMKToken *token,
    CCUINT64 limit, CCUINT64 *out
)
{
    CCUINT64 value = 0;
    for (CCUINT32 i = 0; i < token->len; i++)
    {
        char c = token->text[i];
        if (!parser_is_digit(c))
            return parser_error_at(
                parser, token->line, token->col, "整数字面量非法"
            );
        CCUINT64 digit = (CCUINT64)(c - '0');
        if (value > (limit - digit) / 10)
            return parser_error_at(
                parser, token->line, token->col, "整数超出范围"
            );
        value = value * 10 + digit;
    }

    *out = value;
    return CCTRUE;
}

static CCBOOL parser_int_value(
    CMKParser *parser, const CMKToken *token, CCINT64 *out
)
{
    CCUINT64 value = 0;
    if (!parser_int_magnitude(
            parser, token, (CCUINT64)0x7FFFFFFFFFFFFFFFu, &value
        ))
        return CCFALSE;
    *out = (CCINT64)value;
    return CCTRUE;
}

static CCBOOL parser_float_value(
    CMKParser *parser, const CMKToken *token, double *out
)
{
    const char *text = token->text;
    CCUINT32 len = token->len;
    CCUINT32 i = 0;
    CCBOOL has_digits = CCFALSE;
    long double value = 0.0L;

    while (i < len && parser_is_digit(text[i]))
    {
        value = value * 10.0L + (long double)(text[i] - '0');
        if (!isfinite(value))
            return parser_error_at(
                parser, token->line, token->col, "浮点数超出范围"
            );
        has_digits = CCTRUE;
        i++;
    }

    if (i < len && text[i] == '.')
    {
        i++;
        long double scale = 0.1L;
        while (i < len && parser_is_digit(text[i]))
        {
            value += (long double)(text[i] - '0') * scale;
            if (!isfinite(value))
                return parser_error_at(
                    parser, token->line, token->col, "浮点数超出范围"
                );
            has_digits = CCTRUE;
            if (scale > 0.0L)
                scale *= 0.1L;
            i++;
        }
    }

    CCBOOL exp_negative = CCFALSE;
    CCUINT32 exponent = 0;
    CCBOOL exp_overflow = CCFALSE;
    if (i < len && (text[i] == 'e' || text[i] == 'E'))
    {
        i++;
        if (i < len && (text[i] == '+' || text[i] == '-'))
        {
            exp_negative = (text[i] == '-') ? CCTRUE : CCFALSE;
            i++;
        }
        if (i >= len || !parser_is_digit(text[i]))
            return parser_error_at(
                parser, token->line, token->col,
                "浮点数字面量非法"
            );
        while (i < len && parser_is_digit(text[i]))
        {
            if (exponent < 100000u)
                exponent = exponent * 10 + (CCUINT32)(text[i] - '0');
            else
                exp_overflow = CCTRUE;
            i++;
        }
    }

    if (i != len || !has_digits)
        return parser_error_at(
            parser, token->line, token->col, "浮点数字面量非法"
        );

    if (value == 0.0L)
    {
        *out = 0.0;
        return CCTRUE;
    }

    if (exp_overflow || exponent > 10000u)
        return parser_error_at(
            parser, token->line, token->col,
            exp_negative ? "浮点数下溢" : "浮点数超出范围"
        );

    long double factor = 1.0L;
    for (CCUINT32 n = 0; n < exponent; n++)
    {
        factor *= 10.0L;
        if (!isfinite(factor))
            return parser_error_at(
                parser, token->line, token->col,
                exp_negative ? "浮点数下溢" : "浮点数超出范围"
            );
    }

    if (exp_negative)
        value /= factor;
    else
        value *= factor;

    if (!isfinite(value))
        return parser_error_at(
            parser, token->line, token->col, "浮点数超出范围"
        );

    double result = (double)value;
    if (!isfinite(result))
        return parser_error_at(
            parser, token->line, token->col, "浮点数超出范围"
        );
    if (result == 0.0)
        return parser_error_at(
            parser, token->line, token->col, "浮点数下溢"
        );

    *out = result;
    return CCTRUE;
}

/* ============================================================================
 * 二元运算符
 * ========================================================================== */

static CCBOOL parser_binary_operator(
    CMKTokenType type, CMKBinaryOp *op,
    CCUINT32 *lbp, CCBOOL *relational
)
{
    *relational = CCFALSE;
    switch (type)
    {
    case CMK_TOK_OR:
        *op = CMK_BINARY_OR;
        *lbp = 10;
        break;

    case CMK_TOK_AND:
        *op = CMK_BINARY_AND;
        *lbp = 20;
        break;

    case CMK_TOK_EQ:
        *op = CMK_BINARY_EQ;
        *lbp = 30;
        break;

    case CMK_TOK_NE:
        *op = CMK_BINARY_NE;
        *lbp = 30;
        break;

    case CMK_TOK_LT:
        *op = CMK_BINARY_LT;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case CMK_TOK_LE:
        *op = CMK_BINARY_LE;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case CMK_TOK_GT:
        *op = CMK_BINARY_GT;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case CMK_TOK_GE:
        *op = CMK_BINARY_GE;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case CMK_TOK_PLUS:
        *op = CMK_BINARY_ADD;
        *lbp = 50;
        break;

    case CMK_TOK_MINUS:
        *op = CMK_BINARY_SUB;
        *lbp = 50;
        break;

    case CMK_TOK_STAR:
        *op = CMK_BINARY_MUL;
        *lbp = 60;
        break;

    case CMK_TOK_SLASH:
        *op = CMK_BINARY_DIV;
        *lbp = 60;
        break;

    case CMK_TOK_PERCENT:
        *op = CMK_BINARY_MOD;
        *lbp = 60;
        break;

    default:
        return CCFALSE;
    }
    return CCTRUE;
}

/* ============================================================================
 * 基础表达式
 * ========================================================================== */

static CMKASTNodeId parser_parse_literal(CMKParser *parser)
{
    CMKToken token = parser->current;
    CMKSourceSpan span = parser_token_span(&token);
    CMKASTKind kind = CMK_AST_LITERAL;
    CMKLiteralKind literal_kind = CMK_LITERAL_INT;
    CCINT64 integer = 0;
    double floating = 0.0;
    CCBOOL boolean = CCFALSE;
    CMKASTStringId string_id = CMK_AST_STRING_NIL;

    switch (token.type)
    {
    case CMK_TOK_INT:
        literal_kind = CMK_LITERAL_INT;
        if (!parser_int_value(parser, &token, &integer))
            return CMK_AST_NODE_NIL;
        break;

    case CMK_TOK_FLOAT:
        literal_kind = CMK_LITERAL_FLOAT;
        if (!parser_float_value(parser, &token, &floating))
            return CMK_AST_NODE_NIL;
        break;

    case CMK_TOK_STRING:
        literal_kind = CMK_LITERAL_STRING;
        string_id = parser_new_string(
            parser, token.string, token.string_len
        );
        if (string_id == CMK_AST_STRING_NIL)
            return CMK_AST_NODE_NIL;
        break;

    case CMK_TOK_KW_TRUE:
    case CMK_TOK_KW_FALSE:
        literal_kind = CMK_LITERAL_BOOL;
        boolean = (token.type == CMK_TOK_KW_TRUE) ? CCTRUE : CCFALSE;
        break;

    default:
        return parser_error_current(parser, "期望字面量"),
            CMK_AST_NODE_NIL;
    }

    CMKASTNodeId id = parser_new_node(parser, kind, &span);
    if (id == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CMKASTNode *node = cmk_ast_node_mut(parser->ast, id);
    if (!node)
        return CMK_AST_NODE_NIL;
    node->as.literal.kind = literal_kind;

    switch (literal_kind)
    {
    case CMK_LITERAL_INT:
        node->as.literal.value.integer = integer;
        break;

    case CMK_LITERAL_FLOAT:
        node->as.literal.value.floating = floating;
        break;

    case CMK_LITERAL_STRING:
        node->as.literal.value.string = string_id;
        break;

    case CMK_LITERAL_BOOL:
        node->as.literal.value.boolean = boolean;
        break;

    default:
        break;
    }

    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;
    return id;
}

static CMKASTNodeId parser_parse_config_ref(CMKParser *parser)
{
    CMKToken token = parser->current;
    if (!parser_config_path_ok(token.text, token.len))
        return parser_error_current(parser, "配置引用路径非法"),
            CMK_AST_NODE_NIL;

    CMKASTStringId path = parser_new_string(
        parser, token.text, token.len
    );
    if (path == CMK_AST_STRING_NIL)
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_token_span(&token);
    CMKASTNodeId id = parser_new_node(
        parser, CMK_AST_CONFIG_REF, &span
    );
    if (id == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CMKASTNode *node = cmk_ast_node_mut(parser->ast, id);
    if (!node)
        return CMK_AST_NODE_NIL;
    node->as.config_ref.path = path;

    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;
    return id;
}

typedef struct cmk_unary_prefix {
    CMKToken token;
    CMKUnaryOp op;
} CMKUnaryPrefix;

typedef enum cmk_expr_task {
    CMK_EXPR_TASK_EXPR = 0,
    CMK_EXPR_TASK_BINARY,
    CMK_EXPR_TASK_UNARY,
    CMK_EXPR_TASK_POSTFIX,
    CMK_EXPR_TASK_PRIMARY
} CMKExprTask;

typedef enum cmk_expr_stage {
    CMK_EXPR_STAGE_START = 0,
    CMK_EXPR_STAGE_LEFT,
    CMK_EXPR_STAGE_LOOP,
    CMK_EXPR_STAGE_RIGHT,
    CMK_EXPR_STAGE_BASE,
    CMK_EXPR_STAGE_PRIMARY,
    CMK_EXPR_STAGE_INDEX,
    CMK_EXPR_STAGE_PAREN,
    CMK_EXPR_STAGE_CALL,
    CMK_EXPR_STAGE_CALL_ARG,
    CMK_EXPR_STAGE_LIST,
    CMK_EXPR_STAGE_LIST_ELEM
} CMKExprStage;

typedef struct cmk_expr_frame {
    CMKExprTask task;
    CMKExprStage stage;
    CMKToken start;
    CMKToken callee_token;
    CMKASTNodeId node;
    CCUINT32 min_bp;
    CCBOOL relational_seen;
    CMKBinaryOp op;
    CMKASTStringId callee;
    CCUINT32 count;
    CCDyn prefixes;
    CCUINT32 prefix_count;
    CCUINT32 entered;
} CMKExprFrame;

static void parser_expr_frame_cleanup(
    CMKParser *parser, CMKExprFrame *frame
)
{
    for (CCUINT32 i = 0; i < frame->entered; i++)
        parser_leave_expression(parser);
    frame->entered = 0;
    if (frame->prefixes.array)
        ccalloc(frame->prefixes.array, 0, NULL);
    memset(&frame->prefixes, 0, sizeof(frame->prefixes));
    frame->prefix_count = 0;
}

static CMKExprFrame *parser_expr_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(CMKExprFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKExprFrame);
    return &((CMKExprFrame *)frames->array)[count - 1];
}

static CCBOOL parser_expr_push(
    CMKParser *parser, CCDyn *frames, const CMKExprFrame *frame
)
{
    if (!cmk_dyn_push(
            frames, (CCUINT32)sizeof(CMKExprFrame),
            (CCINT32)sizeof(CMKExprFrame), frame
        ))
    {
        parser_expr_frame_cleanup(parser, (CMKExprFrame *)frame);
        (void)parser_error_current(parser, "内存分配失败");
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL parser_expr_pop(CCDyn *frames, CMKExprFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(CMKExprFrame),
        (CCINT32)sizeof(CMKExprFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void parser_expr_frames_abort(CMKParser *parser, CCDyn *frames)
{
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKExprFrame);
    for (CCUINT32 i = count; i > 0; i--)
        parser_expr_frame_cleanup(
            parser, &((CMKExprFrame *)frames->array)[i - 1]
        );
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL parser_expr_binary_loop(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CMKASTNodeId *completed, CCBOOL *done
)
{
    for (;;)
    {
        CMKBinaryOp op;
        CCUINT32 lbp = 0;
        CCBOOL relational = CCFALSE;
        if (!parser_binary_operator(
                parser->current.type, &op, &lbp, &relational
            ))
        {
            *completed = frame->node;
            *done = CCTRUE;
            return CCTRUE;
        }
        if (lbp < frame->min_bp)
        {
            *completed = frame->node;
            *done = CCTRUE;
            return CCTRUE;
        }
        if (relational && frame->relational_seen)
            return parser_error_current(
                parser, "不支持链式比较"
            ), CCFALSE;
        if (relational)
            frame->relational_seen = CCTRUE;
        if (!parser_advance(parser))
            return CCFALSE;

        frame->op = op;
        frame->stage = CMK_EXPR_STAGE_RIGHT;
        CMKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = CMK_EXPR_TASK_BINARY;
        child.stage = CMK_EXPR_STAGE_START;
        child.min_bp = lbp + 1;
        child.start = parser->current;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
}

static CCBOOL parser_expr_finish_unary(
    CMKParser *parser, CMKExprFrame *frame, CMKASTNodeId base,
    CMKASTNodeId *completed
)
{
    CMKUnaryPrefix *items = (CMKUnaryPrefix *)frame->prefixes.array;
    for (CCUINT32 i = frame->prefix_count; i > 0; i--)
    {
        CMKUnaryPrefix *prefix = &items[i - 1];
        CMKSourceSpan span =
            parser_make_span(&prefix->token, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_UNARY, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CCFALSE;
        CMKASTNode *unary = cmk_ast_node_mut(parser->ast, node);
        if (!unary)
            return CCFALSE;
        unary->as.unary.op = prefix->op;
        if (!parser_append_child(parser, node, base))
            return CCFALSE;
        base = node;
    }
    *completed = base;
    return CCTRUE;
}

static CCBOOL parser_expr_task_expr(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input)
    {
        if (!parser_enter_expression(parser))
            return CCFALSE;
        frame->entered++;
        frame->stage = CMK_EXPR_STAGE_LEFT;
        CMKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = CMK_EXPR_TASK_BINARY;
        child.stage = CMK_EXPR_STAGE_START;
        child.min_bp = 0;
        child.start = parser->current;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
    *completed = input;
    *done = CCTRUE;
    return CCTRUE;
}

static CCBOOL parser_expr_task_binary(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input)
    {
        CMKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = CMK_EXPR_TASK_UNARY;
        child.stage = CMK_EXPR_STAGE_START;
        frame->stage = CMK_EXPR_STAGE_LEFT;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
    if (frame->stage == CMK_EXPR_STAGE_LEFT)
    {
        frame->node = input;
        frame->stage = CMK_EXPR_STAGE_LOOP;
        return parser_expr_binary_loop(
            parser, frame, frames, completed, done
        );
    }
    if (frame->stage == CMK_EXPR_STAGE_RIGHT)
    {
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_BINARY, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CCFALSE;
        CMKASTNode *binary = cmk_ast_node_mut(parser->ast, node);
        if (!binary)
            return CCFALSE;
        binary->as.binary.op = frame->op;
        if (!parser_append_child(parser, node, frame->node) ||
            !parser_append_child(parser, node, input))
            return CCFALSE;
        frame->node = node;
        frame->stage = CMK_EXPR_STAGE_LOOP;
        return parser_expr_binary_loop(
            parser, frame, frames, completed, done
        );
    }
    return CCFALSE;
}

static CCBOOL parser_expr_task_unary(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
{
    if (!has_input)
    {
        while (parser->current.type == CMK_TOK_NOT ||
               parser->current.type == CMK_TOK_MINUS ||
               parser->current.type == CMK_TOK_PLUS)
        {
            CMKTokenType type = parser->current.type;
            CMKUnaryPrefix prefix;
            memset(&prefix, 0, sizeof(prefix));
            prefix.token = parser->current;
            prefix.op = (type == CMK_TOK_NOT) ? CMK_UNARY_NOT
                : (type == CMK_TOK_MINUS) ? CMK_UNARY_NEG
                : CMK_UNARY_POS;
            if (!parser_enter_expression(parser))
                return CCFALSE;
            frame->entered++;
            if (!cmk_dyn_push(
                    &frame->prefixes,
                    (CCUINT32)sizeof(CMKUnaryPrefix),
                    (CCINT32)sizeof(CMKUnaryPrefix), &prefix
                ))
            {
                (void)parser_error_current(parser, "内存分配失败");
                return CCFALSE;
            }
            frame->prefix_count++;
            if (!parser_advance(parser))
                return CCFALSE;
        }

        if (frame->prefix_count == 0)
        {
            frame->stage = CMK_EXPR_STAGE_BASE;
            CMKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = CMK_EXPR_TASK_POSTFIX;
            child.stage = CMK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        CMKUnaryPrefix *items =
            (CMKUnaryPrefix *)frame->prefixes.array;
        if (items[frame->prefix_count - 1].op == CMK_UNARY_NEG &&
            parser->current.type == CMK_TOK_INT)
        {
            CCUINT64 magnitude = 0;
            if (!parser_int_magnitude(
                    parser, &parser->current,
                    (CCUINT64)0x8000000000000000u, &magnitude
                ))
                return CCFALSE;
            if (magnitude == (CCUINT64)0x8000000000000000u)
            {
                CMKToken int_token = parser->current;
                CMKSourceSpan span = parser_make_span(
                    &items[frame->prefix_count - 1].token,
                    &int_token
                );
                CMKASTNodeId base = parser_new_node(
                    parser, CMK_AST_LITERAL, &span
                );
                if (base == CMK_AST_NODE_NIL)
                    return CCFALSE;
                CMKASTNode *literal =
                    cmk_ast_node_mut(parser->ast, base);
                if (!literal)
                    return CCFALSE;
                literal->as.literal.kind = CMK_LITERAL_INT;
                literal->as.literal.value.integer =
                    -((CCINT64)0x7FFFFFFFFFFFFFFF) - 1;
                if (!parser_advance(parser))
                    return CCFALSE;
                frame->prefix_count--;
                parser_leave_expression(parser);
                frame->entered--;
                if (!parser_expr_finish_unary(
                        parser, frame, base, completed
                    ))
                    return CCFALSE;
                *done = CCTRUE;
                return CCTRUE;
            }
        }

        frame->stage = CMK_EXPR_STAGE_BASE;
        CMKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = CMK_EXPR_TASK_POSTFIX;
        child.stage = CMK_EXPR_STAGE_START;
        child.start = parser->current;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    if (!parser_expr_finish_unary(parser, frame, input, completed))
        return CCFALSE;
    *done = CCTRUE;
    return CCTRUE;
}
}

static CCBOOL parser_expr_task_postfix(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input)
    {
        CMKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = CMK_EXPR_TASK_PRIMARY;
        child.stage = CMK_EXPR_STAGE_START;
        child.start = parser->current;
        frame->stage = CMK_EXPR_STAGE_PRIMARY;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
    if (frame->stage == CMK_EXPR_STAGE_PRIMARY)
    {
        frame->node = input;
        frame->stage = CMK_EXPR_STAGE_LOOP;
    }
    else if (frame->stage == CMK_EXPR_STAGE_INDEX)
    {
        if (!parser_expect(parser, CMK_TOK_RBRACKET, "右方括号"))
            return CCFALSE;
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_INDEX, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CCFALSE;
        if (!parser_append_child(parser, node, frame->node) ||
            !parser_append_child(parser, node, input))
            return CCFALSE;
        frame->node = node;
        frame->stage = CMK_EXPR_STAGE_LOOP;
    }

    for (;;)
    {
        if (parser->current.type == CMK_TOK_LBRACKET)
        {
            if (!parser_advance(parser))
                return CCFALSE;
            frame->stage = CMK_EXPR_STAGE_INDEX;
            CMKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = CMK_EXPR_TASK_EXPR;
            child.stage = CMK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }
        if (parser->current.type == CMK_TOK_DOT)
        {
            if (!parser_advance(parser))
                return CCFALSE;
            if (parser->current.type != CMK_TOK_IDENT)
                return parser_error_current(
                    parser, "成员访问需要标识符"
                ), CCFALSE;
            CMKASTStringId member = parser_new_string(
                parser, parser->current.text, parser->current.len
            );
            if (member == CMK_AST_STRING_NIL)
                return CCFALSE;
            if (!parser_advance(parser))
                return CCFALSE;
            CMKSourceSpan span =
                parser_make_span(&frame->start, &parser->previous);
            CMKASTNodeId node = parser_new_node(
                parser, CMK_AST_MEMBER, &span
            );
            if (node == CMK_AST_NODE_NIL)
                return CCFALSE;
            CMKASTNode *member_node =
                cmk_ast_node_mut(parser->ast, node);
            if (!member_node)
                return CCFALSE;
            member_node->as.member.member = member;
            if (!parser_append_child(parser, node, frame->node))
                return CCFALSE;
            frame->node = node;
            continue;
        }
        *completed = frame->node;
        *done = CCTRUE;
        return CCTRUE;
    }
}

static CCBOOL parser_expr_primary_resume(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
);

static CCBOOL parser_expr_primary_start(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
    (void)input;
    if (!has_input && frame->stage == CMK_EXPR_STAGE_START)
    {
        switch (parser->current.type)
        {
        case CMK_TOK_INT:
        case CMK_TOK_FLOAT:
        case CMK_TOK_STRING:
        case CMK_TOK_KW_TRUE:
        case CMK_TOK_KW_FALSE:
            *completed = parser_parse_literal(parser);
            *done = CCTRUE;
            return (*completed != CMK_AST_NODE_NIL)
                ? CCTRUE : CCFALSE;

        case CMK_TOK_CONFIG_REF:
            *completed = parser_parse_config_ref(parser);
            *done = CCTRUE;
            return (*completed != CMK_AST_NODE_NIL)
                ? CCTRUE : CCFALSE;

        case CMK_TOK_IDENT:
        {
            CMKToken token = parser->current;
            CMKASTStringId name = parser_new_string(
                parser, token.text, token.len
            );
            if (name == CMK_AST_STRING_NIL)
                return CCFALSE;
            if (!parser_advance(parser))
                return CCFALSE;

            if (parser->current.type == CMK_TOK_LPAREN)
            {
                if (!parser_advance(parser))
                    return CCFALSE;
                CMKASTNodeId call = parser_new_node(
                    parser, CMK_AST_CALL, NULL
                );
                if (call == CMK_AST_NODE_NIL)
                    return CCFALSE;
                CMKASTNode *node =
                    cmk_ast_node_mut(parser->ast, call);
                if (!node)
                    return CCFALSE;
                node->as.call.callee = name;
                frame->node = call;
                frame->callee = name;
                frame->callee_token = token;
                frame->stage = CMK_EXPR_STAGE_CALL;
                break;
            }

            CMKSourceSpan span = parser_token_span(&token);
            CMKASTNodeId id = parser_new_node(
                parser, CMK_AST_IDENT, &span
            );
            if (id == CMK_AST_NODE_NIL)
                return CCFALSE;
            CMKASTNode *node =
                cmk_ast_node_mut(parser->ast, id);
            if (!node)
                return CCFALSE;
            node->as.ident.name = name;
            *completed = id;
            *done = CCTRUE;
            return CCTRUE;
        }

        case CMK_TOK_LPAREN:
        {
            CMKToken start = parser->current;
            if (!parser_advance(parser))
                return CCFALSE;
            frame->start = start;
            frame->stage = CMK_EXPR_STAGE_PAREN;
            CMKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = CMK_EXPR_TASK_EXPR;
            child.stage = CMK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        case CMK_TOK_LBRACE:
        {
            frame->start = parser->current;
            if (!parser_expect(
                    parser, CMK_TOK_LBRACE, "左花括号"
                ))
                return CCFALSE;
            CMKASTNodeId list = parser_new_node(
                parser, CMK_AST_LIST, NULL
            );
            if (list == CMK_AST_NODE_NIL)
                return CCFALSE;
            frame->node = list;
            frame->stage = CMK_EXPR_STAGE_LIST;
            break;
        }

        case CMK_TOK_UNDERSCORE:
            return parser_error_current(
                parser, "`_` 只能作为赋值目标"
            ), CCFALSE;

        default:
            return parser_error_current(
                parser, "期望表达式，实际%s",
                cmk_token_type_name(parser->current.type)
            ), CCFALSE;
        }
    }
    return parser_expr_primary_resume(
        parser, frame, frames, has_input, input, completed, done
    );
}

static CCBOOL parser_expr_primary_resume(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
if (frame->stage == CMK_EXPR_STAGE_PAREN)
    {
        if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
            return CCFALSE;
        CMKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        if (!parser_set_node_span(parser, input, &span))
            return CCFALSE;
        *completed = input;
        *done = CCTRUE;
        return CCTRUE;
    }

    if (frame->stage == CMK_EXPR_STAGE_CALL ||
        frame->stage == CMK_EXPR_STAGE_CALL_ARG)
    {
        if (has_input)
        {
            if (!parser_append_child(parser, frame->node, input))
                return CCFALSE;
            frame->count++;
            if (parser->current.type == CMK_TOK_COMMA)
            {
                if (!parser_advance(parser))
                    return CCFALSE;
                if (parser->current.type == CMK_TOK_RPAREN)
                    return parser_error_current(
                        parser, "函数调用实参不允许尾逗号"
                    ), CCFALSE;
                frame->stage = CMK_EXPR_STAGE_CALL_ARG;
                CMKExprFrame child;
                memset(&child, 0, sizeof(child));
                child.task = CMK_EXPR_TASK_EXPR;
                child.stage = CMK_EXPR_STAGE_START;
                child.start = parser->current;
                if (!parser_expr_push(parser, frames, &child))
                    return CCFALSE;
                *done = CCFALSE;
                return CCTRUE;
            }
        }
        else if (parser->current.type != CMK_TOK_RPAREN)
        {
            frame->stage = CMK_EXPR_STAGE_CALL_ARG;
            CMKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = CMK_EXPR_TASK_EXPR;
            child.stage = CMK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
            return CCFALSE;
        CMKSourceSpan span =
            parser_make_span(&frame->callee_token, &parser->previous);
        if (!parser_set_node_span(parser, frame->node, &span))
            return CCFALSE;
        *completed = frame->node;
        *done = CCTRUE;
        return CCTRUE;
    }

    if (frame->stage == CMK_EXPR_STAGE_LIST ||
        frame->stage == CMK_EXPR_STAGE_LIST_ELEM)
    {
        if (has_input)
        {
            if (!parser_append_child(parser, frame->node, input))
                return CCFALSE;
            frame->count++;
            if (parser->current.type == CMK_TOK_COMMA)
            {
                if (!parser_advance(parser))
                    return CCFALSE;
            }
            else if (parser->current.type != CMK_TOK_RBRACE)
            {
                return parser_error_current(
                    parser, "列表元素之间需要逗号或右花括号"
                ), CCFALSE;
            }
        }

        if (parser->current.type == CMK_TOK_RBRACE)
        {
            if (!parser_expect(parser, CMK_TOK_RBRACE, "右花括号"))
                return CCFALSE;
            CMKSourceSpan span =
                parser_make_span(&frame->start, &parser->previous);
            if (!parser_set_node_span(parser, frame->node, &span))
                return CCFALSE;
            *completed = frame->node;
            *done = CCTRUE;
            return CCTRUE;
        }

        if (parser->limits.max_list_elements != 0 &&
            frame->count >= parser->limits.max_list_elements)
            return parser_error_current(
                parser, "列表元素数量超过限制"
            ), CCFALSE;

        frame->stage = CMK_EXPR_STAGE_LIST_ELEM;
        CMKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = CMK_EXPR_TASK_EXPR;
        child.stage = CMK_EXPR_STAGE_START;
        child.start = parser->current;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    return CCFALSE;
}

static CCBOOL parser_expr_task_primary(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input && frame->stage == CMK_EXPR_STAGE_START)
        return parser_expr_primary_start(
            parser, frame, frames, has_input, input, completed, done
        );
    return parser_expr_primary_resume(
        parser, frame, frames, has_input, input, completed, done
    );
}

static CCBOOL parser_expr_step(
    CMKParser *parser, CMKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKASTNodeId input,
    CMKASTNodeId *completed, CCBOOL *done
)
{
    switch (frame->task)
    {
    case CMK_EXPR_TASK_EXPR:
        return parser_expr_task_expr(
            parser, frame, frames, has_input, input, completed, done
        );
    case CMK_EXPR_TASK_BINARY:
        return parser_expr_task_binary(
            parser, frame, frames, has_input, input, completed, done
        );
    case CMK_EXPR_TASK_UNARY:
        return parser_expr_task_unary(
            parser, frame, frames, has_input, input, completed, done
        );
    case CMK_EXPR_TASK_POSTFIX:
        return parser_expr_task_postfix(
            parser, frame, frames, has_input, input, completed, done
        );
    case CMK_EXPR_TASK_PRIMARY:
        return parser_expr_task_primary(
            parser, frame, frames, has_input, input, completed, done
        );
    default:
        return CCFALSE;
    }
}

static CMKASTNodeId parser_expr_run(CMKParser *parser)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CMKASTNodeId completed = CMK_AST_NODE_NIL;
    CCBOOL has_completed = CCFALSE;

    for (;;)
    {
        if (has_completed)
        {
            CMKExprFrame *frame = parser_expr_top(&frames);
            if (!frame)
            {
                ccalloc(frames.array, 0, NULL);
                return completed;
            }
            CMKASTNodeId result = CMK_AST_NODE_NIL;
            CCBOOL done = CCFALSE;
            if (!parser_expr_step(
                    parser, frame, &frames, CCTRUE, completed,
                    &result, &done
                ))
                goto Failed;
            if (done)
            {
                parser_expr_frame_cleanup(parser, frame);
                CMKExprFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!parser_expr_pop(&frames, &popped))
                    goto Failed;
                completed = result;
            }
            else
            {
                has_completed = CCFALSE;
                continue;
            }
            continue;
        }

        if (frames.size == 0)
        {
            CMKExprFrame start;
            memset(&start, 0, sizeof(start));
            start.task = CMK_EXPR_TASK_EXPR;
            start.stage = CMK_EXPR_STAGE_START;
            start.start = parser->current;
            if (!parser_expr_push(parser, &frames, &start))
                goto Failed;
            continue;
        }

        CMKExprFrame *frame = parser_expr_top(&frames);
        if (!frame)
            goto Failed;
        CMKASTNodeId result = CMK_AST_NODE_NIL;
        CCBOOL done = CCFALSE;
        if (!parser_expr_step(
                parser, frame, &frames, CCFALSE, CMK_AST_NODE_NIL,
                &result, &done
            ))
            goto Failed;
        if (done)
        {
            parser_expr_frame_cleanup(parser, frame);
            CMKExprFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!parser_expr_pop(&frames, &popped))
                goto Failed;
            completed = result;
            has_completed = CCTRUE;
        }
    }

Failed:
    parser_expr_frames_abort(parser, &frames);
    return CMK_AST_NODE_NIL;
}


CMKASTNodeId parser_parse_expression(CMKParser *parser)
{
    if (!parser || parser->failed)
        return CMK_AST_NODE_NIL;
    return parser_expr_run(parser);
}
