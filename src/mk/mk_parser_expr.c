/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 表达式 Parser：Pratt、primary、postfix、list 与字面量
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_parser_internal.h"
#include "mk_safe.h"

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
    MKParser *parser, const MKToken *token,
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
    MKParser *parser, const MKToken *token, CCINT64 *out
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
    MKParser *parser, const MKToken *token, double *out
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
    MKTokenType type, MKBinaryOp *op,
    CCUINT32 *lbp, CCBOOL *relational
)
{
    *relational = CCFALSE;
    switch (type)
    {
    case MK_TOK_OR:
        *op = MK_BINARY_OR;
        *lbp = 10;
        break;

    case MK_TOK_AND:
        *op = MK_BINARY_AND;
        *lbp = 20;
        break;

    case MK_TOK_EQ:
        *op = MK_BINARY_EQ;
        *lbp = 30;
        break;

    case MK_TOK_NE:
        *op = MK_BINARY_NE;
        *lbp = 30;
        break;

    case MK_TOK_LT:
        *op = MK_BINARY_LT;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case MK_TOK_LE:
        *op = MK_BINARY_LE;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case MK_TOK_GT:
        *op = MK_BINARY_GT;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case MK_TOK_GE:
        *op = MK_BINARY_GE;
        *lbp = 40;
        *relational = CCTRUE;
        break;

    case MK_TOK_PLUS:
        *op = MK_BINARY_ADD;
        *lbp = 50;
        break;

    case MK_TOK_MINUS:
        *op = MK_BINARY_SUB;
        *lbp = 50;
        break;

    case MK_TOK_STAR:
        *op = MK_BINARY_MUL;
        *lbp = 60;
        break;

    case MK_TOK_SLASH:
        *op = MK_BINARY_DIV;
        *lbp = 60;
        break;

    case MK_TOK_PERCENT:
        *op = MK_BINARY_MOD;
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

static MKASTNodeId parser_parse_literal(MKParser *parser)
{
    MKToken token = parser->current;
    MKSourceSpan span = parser_token_span(&token);
    MKASTKind kind = MK_AST_LITERAL;
    MKLiteralKind literal_kind = MK_LITERAL_INT;
    CCINT64 integer = 0;
    double floating = 0.0;
    CCBOOL boolean = CCFALSE;
    MKASTStringId string_id = MK_AST_STRING_NIL;

    switch (token.type)
    {
    case MK_TOK_INT:
        literal_kind = MK_LITERAL_INT;
        if (!parser_int_value(parser, &token, &integer))
            return MK_AST_NODE_NIL;
        break;

    case MK_TOK_FLOAT:
        literal_kind = MK_LITERAL_FLOAT;
        if (!parser_float_value(parser, &token, &floating))
            return MK_AST_NODE_NIL;
        break;

    case MK_TOK_STRING:
        literal_kind = MK_LITERAL_STRING;
        string_id = parser_new_string(
            parser, token.string, token.string_len
        );
        if (string_id == MK_AST_STRING_NIL)
            return MK_AST_NODE_NIL;
        break;

    case MK_TOK_KW_TRUE:
    case MK_TOK_KW_FALSE:
        literal_kind = MK_LITERAL_BOOL;
        boolean = (token.type == MK_TOK_KW_TRUE) ? CCTRUE : CCFALSE;
        break;

    default:
        (void)parser_error_current(parser, "期望字面量");
        return MK_AST_NODE_NIL;
    }

    MKASTNodeId id = parser_new_node(parser, kind, &span);
    if (id == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;

    MKASTNode *node = mk_ast_node_mut(parser->ast, id);
    if (!node)
        return MK_AST_NODE_NIL;
    node->as.literal.kind = literal_kind;

    switch (literal_kind)
    {
    case MK_LITERAL_INT:
        node->as.literal.value.integer = integer;
        break;

    case MK_LITERAL_FLOAT:
        node->as.literal.value.floating = floating;
        break;

    case MK_LITERAL_STRING:
        node->as.literal.value.string = string_id;
        break;

    case MK_LITERAL_BOOL:
        node->as.literal.value.boolean = boolean;
        break;

    default:
        break;
    }

    if (!parser_advance(parser))
        return MK_AST_NODE_NIL;
    return id;
}

static MKASTNodeId parser_parse_config_ref(MKParser *parser)
{
    MKToken token = parser->current;
    if (!parser_config_path_ok(token.text, token.len))
    {
        (void)parser_error_current(parser, "配置引用路径非法");
        return MK_AST_NODE_NIL;
    }

    MKASTStringId path = parser_new_string(
        parser, token.text, token.len
    );
    if (path == MK_AST_STRING_NIL)
        return MK_AST_NODE_NIL;

    MKSourceSpan span = parser_token_span(&token);
    MKASTNodeId id = parser_new_node(
        parser, MK_AST_CONFIG_REF, &span
    );
    if (id == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;

    MKASTNode *node = mk_ast_node_mut(parser->ast, id);
    if (!node)
        return MK_AST_NODE_NIL;
    node->as.config_ref.path = path;

    if (!parser_advance(parser))
        return MK_AST_NODE_NIL;
    return id;
}

typedef struct mk_unary_prefix {
    MKToken token;
    MKUnaryOp op;
} MKUnaryPrefix;

typedef enum mk_expr_task {
    MK_EXPR_TASK_EXPR = 0,
    MK_EXPR_TASK_BINARY,
    MK_EXPR_TASK_UNARY,
    MK_EXPR_TASK_POSTFIX,
    MK_EXPR_TASK_PRIMARY
} MKExprTask;

typedef enum mk_expr_stage {
    MK_EXPR_STAGE_START = 0,
    MK_EXPR_STAGE_LEFT,
    MK_EXPR_STAGE_LOOP,
    MK_EXPR_STAGE_RIGHT,
    MK_EXPR_STAGE_BASE,
    MK_EXPR_STAGE_PRIMARY,
    MK_EXPR_STAGE_INDEX,
    MK_EXPR_STAGE_PAREN,
    MK_EXPR_STAGE_CALL,
    MK_EXPR_STAGE_CALL_ARG,
    MK_EXPR_STAGE_LIST,
    MK_EXPR_STAGE_LIST_ELEM
} MKExprStage;

typedef struct mk_expr_frame {
    MKExprTask task;
    MKExprStage stage;
    MKToken start;
    MKToken callee_token;
    MKASTNodeId node;
    CCUINT32 min_bp;
    CCBOOL relational_seen;
    MKBinaryOp op;
    MKASTStringId callee;
    CCUINT32 count;
    CCDyn prefixes;
    CCUINT32 prefix_count;
    CCUINT32 entered;
} MKExprFrame;

static void parser_expr_frame_cleanup(
    MKParser *parser, MKExprFrame *frame
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

static MKExprFrame *parser_expr_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(MKExprFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKExprFrame);
    return &((MKExprFrame *)frames->array)[count - 1];
}

static CCBOOL parser_expr_push(
    MKParser *parser, CCDyn *frames, const MKExprFrame *frame
)
{
    if (!mk_dyn_push(
            frames, (CCUINT32)sizeof(MKExprFrame),
            (CCINT32)sizeof(MKExprFrame), frame
        ))
    {
        parser_expr_frame_cleanup(parser, (MKExprFrame *)frame);
        (void)parser_error_current(parser, "内存分配失败");
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL parser_expr_pop(CCDyn *frames, MKExprFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(MKExprFrame),
        (CCINT32)sizeof(MKExprFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void parser_expr_frames_abort(MKParser *parser, CCDyn *frames)
{
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKExprFrame);
    for (CCUINT32 i = count; i > 0; i--)
        parser_expr_frame_cleanup(
            parser, &((MKExprFrame *)frames->array)[i - 1]
        );
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL parser_expr_binary_loop(
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    MKASTNodeId *completed, CCBOOL *done
)
{
    for (;;)
    {
        MKBinaryOp op;
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
        frame->stage = MK_EXPR_STAGE_RIGHT;
        MKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = MK_EXPR_TASK_BINARY;
        child.stage = MK_EXPR_STAGE_START;
        child.min_bp = lbp + 1;
        child.start = parser->current;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
}

static CCBOOL parser_expr_finish_unary(
    MKParser *parser, MKExprFrame *frame, MKASTNodeId base,
    MKASTNodeId *completed
)
{
    MKUnaryPrefix *items = (MKUnaryPrefix *)frame->prefixes.array;
    for (CCUINT32 i = frame->prefix_count; i > 0; i--)
    {
        MKUnaryPrefix *prefix = &items[i - 1];
        MKSourceSpan span =
            parser_make_span(&prefix->token, &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_UNARY, &span
        );
        if (node == MK_AST_NODE_NIL)
            return CCFALSE;
        MKASTNode *unary = mk_ast_node_mut(parser->ast, node);
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
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input)
    {
        if (!parser_enter_expression(parser))
            return CCFALSE;
        frame->entered++;
        frame->stage = MK_EXPR_STAGE_LEFT;
        MKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = MK_EXPR_TASK_BINARY;
        child.stage = MK_EXPR_STAGE_START;
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
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input)
    {
        MKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = MK_EXPR_TASK_UNARY;
        child.stage = MK_EXPR_STAGE_START;
        frame->stage = MK_EXPR_STAGE_LEFT;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
    if (frame->stage == MK_EXPR_STAGE_LEFT)
    {
        frame->node = input;
        frame->stage = MK_EXPR_STAGE_LOOP;
        return parser_expr_binary_loop(
            parser, frame, frames, completed, done
        );
    }
    if (frame->stage == MK_EXPR_STAGE_RIGHT)
    {
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_BINARY, &span
        );
        if (node == MK_AST_NODE_NIL)
            return CCFALSE;
        MKASTNode *binary = mk_ast_node_mut(parser->ast, node);
        if (!binary)
            return CCFALSE;
        binary->as.binary.op = frame->op;
        if (!parser_append_child(parser, node, frame->node) ||
            !parser_append_child(parser, node, input))
            return CCFALSE;
        frame->node = node;
        frame->stage = MK_EXPR_STAGE_LOOP;
        return parser_expr_binary_loop(
            parser, frame, frames, completed, done
        );
    }
    return CCFALSE;
}

static CCBOOL parser_expr_task_unary(
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
{
    if (!has_input)
    {
        while (parser->current.type == MK_TOK_NOT ||
               parser->current.type == MK_TOK_MINUS ||
               parser->current.type == MK_TOK_PLUS)
        {
            MKTokenType type = parser->current.type;
            MKUnaryPrefix prefix;
            memset(&prefix, 0, sizeof(prefix));
            prefix.token = parser->current;
            prefix.op = (type == MK_TOK_NOT) ? MK_UNARY_NOT
                : (type == MK_TOK_MINUS) ? MK_UNARY_NEG
                : MK_UNARY_POS;
            if (!parser_enter_expression(parser))
                return CCFALSE;
            frame->entered++;
            if (!mk_dyn_push(
                    &frame->prefixes,
                    (CCUINT32)sizeof(MKUnaryPrefix),
                    (CCINT32)sizeof(MKUnaryPrefix), &prefix
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
            frame->stage = MK_EXPR_STAGE_BASE;
            MKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = MK_EXPR_TASK_POSTFIX;
            child.stage = MK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        MKUnaryPrefix *items =
            (MKUnaryPrefix *)frame->prefixes.array;
        if (items[frame->prefix_count - 1].op == MK_UNARY_NEG &&
            parser->current.type == MK_TOK_INT)
        {
            CCUINT64 magnitude = 0;
            if (!parser_int_magnitude(
                    parser, &parser->current,
                    (CCUINT64)0x8000000000000000u, &magnitude
                ))
                return CCFALSE;
            if (magnitude == (CCUINT64)0x8000000000000000u)
            {
                MKToken int_token = parser->current;
                MKSourceSpan span = parser_make_span(
                    &items[frame->prefix_count - 1].token,
                    &int_token
                );
                MKASTNodeId base = parser_new_node(
                    parser, MK_AST_LITERAL, &span
                );
                if (base == MK_AST_NODE_NIL)
                    return CCFALSE;
                MKASTNode *literal =
                    mk_ast_node_mut(parser->ast, base);
                if (!literal)
                    return CCFALSE;
                literal->as.literal.kind = MK_LITERAL_INT;
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

        frame->stage = MK_EXPR_STAGE_BASE;
        MKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = MK_EXPR_TASK_POSTFIX;
        child.stage = MK_EXPR_STAGE_START;
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
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input)
    {
        MKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = MK_EXPR_TASK_PRIMARY;
        child.stage = MK_EXPR_STAGE_START;
        child.start = parser->current;
        frame->stage = MK_EXPR_STAGE_PRIMARY;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
    if (frame->stage == MK_EXPR_STAGE_PRIMARY)
    {
        frame->node = input;
        frame->stage = MK_EXPR_STAGE_LOOP;
    }
    else if (frame->stage == MK_EXPR_STAGE_INDEX)
    {
        if (!parser_expect(parser, MK_TOK_RBRACKET, "右方括号"))
            return CCFALSE;
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        MKASTNodeId node = parser_new_node(
            parser, MK_AST_INDEX, &span
        );
        if (node == MK_AST_NODE_NIL)
            return CCFALSE;
        if (!parser_append_child(parser, node, frame->node) ||
            !parser_append_child(parser, node, input))
            return CCFALSE;
        frame->node = node;
        frame->stage = MK_EXPR_STAGE_LOOP;
    }

    for (;;)
    {
        if (parser->current.type == MK_TOK_LBRACKET)
        {
            if (!parser_advance(parser))
                return CCFALSE;
            frame->stage = MK_EXPR_STAGE_INDEX;
            MKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = MK_EXPR_TASK_EXPR;
            child.stage = MK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }
        if (parser->current.type == MK_TOK_DOT)
        {
            if (!parser_advance(parser))
                return CCFALSE;
            if (parser->current.type != MK_TOK_IDENT)
                return parser_error_current(
                    parser, "成员访问需要标识符"
                ), CCFALSE;
            MKASTStringId member = parser_new_string(
                parser, parser->current.text, parser->current.len
            );
            if (member == MK_AST_STRING_NIL)
                return CCFALSE;
            if (!parser_advance(parser))
                return CCFALSE;
            MKSourceSpan span =
                parser_make_span(&frame->start, &parser->previous);
            MKASTNodeId node = parser_new_node(
                parser, MK_AST_MEMBER, &span
            );
            if (node == MK_AST_NODE_NIL)
                return CCFALSE;
            MKASTNode *member_node =
                mk_ast_node_mut(parser->ast, node);
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
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
);

static CCBOOL parser_expr_primary_start(
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
    (void)input;
    if (!has_input && frame->stage == MK_EXPR_STAGE_START)
    {
        switch (parser->current.type)
        {
        case MK_TOK_INT:
        case MK_TOK_FLOAT:
        case MK_TOK_STRING:
        case MK_TOK_KW_TRUE:
        case MK_TOK_KW_FALSE:
            *completed = parser_parse_literal(parser);
            *done = CCTRUE;
            return (*completed != MK_AST_NODE_NIL)
                ? CCTRUE : CCFALSE;

        case MK_TOK_CONFIG_REF:
            *completed = parser_parse_config_ref(parser);
            *done = CCTRUE;
            return (*completed != MK_AST_NODE_NIL)
                ? CCTRUE : CCFALSE;

        case MK_TOK_IDENT:
        {
            MKToken token = parser->current;
            MKASTStringId name = parser_new_string(
                parser, token.text, token.len
            );
            if (name == MK_AST_STRING_NIL)
                return CCFALSE;
            if (!parser_advance(parser))
                return CCFALSE;

            if (parser->current.type == MK_TOK_LPAREN)
            {
                if (!parser_advance(parser))
                    return CCFALSE;
                MKASTNodeId call = parser_new_node(
                    parser, MK_AST_CALL, NULL
                );
                if (call == MK_AST_NODE_NIL)
                    return CCFALSE;
                MKASTNode *node =
                    mk_ast_node_mut(parser->ast, call);
                if (!node)
                    return CCFALSE;
                node->as.call.callee = name;
                frame->node = call;
                frame->callee = name;
                frame->callee_token = token;
                frame->stage = MK_EXPR_STAGE_CALL;
                break;
            }

            MKSourceSpan span = parser_token_span(&token);
            MKASTNodeId id = parser_new_node(
                parser, MK_AST_IDENT, &span
            );
            if (id == MK_AST_NODE_NIL)
                return CCFALSE;
            MKASTNode *node =
                mk_ast_node_mut(parser->ast, id);
            if (!node)
                return CCFALSE;
            node->as.ident.name = name;
            *completed = id;
            *done = CCTRUE;
            return CCTRUE;
        }

        case MK_TOK_LPAREN:
        {
            MKToken start = parser->current;
            if (!parser_advance(parser))
                return CCFALSE;
            frame->start = start;
            frame->stage = MK_EXPR_STAGE_PAREN;
            MKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = MK_EXPR_TASK_EXPR;
            child.stage = MK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        case MK_TOK_LBRACE:
        {
            frame->start = parser->current;
            if (!parser_expect(
                    parser, MK_TOK_LBRACE, "左花括号"
                ))
                return CCFALSE;
            MKASTNodeId list = parser_new_node(
                parser, MK_AST_LIST, NULL
            );
            if (list == MK_AST_NODE_NIL)
                return CCFALSE;
            frame->node = list;
            frame->stage = MK_EXPR_STAGE_LIST;
            break;
        }

        case MK_TOK_UNDERSCORE:
            return parser_error_current(
                parser, "`_` 只能作为赋值目标"
            ), CCFALSE;

        default:
            return parser_error_current(
                parser, "期望表达式，实际%s",
                mk_token_type_name(parser->current.type)
            ), CCFALSE;
        }
    }
    return parser_expr_primary_resume(
        parser, frame, frames, has_input, input, completed, done
    );
}

static CCBOOL parser_expr_primary_resume(
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
if (frame->stage == MK_EXPR_STAGE_PAREN)
    {
        if (!parser_expect(parser, MK_TOK_RPAREN, "右圆括号"))
            return CCFALSE;
        MKSourceSpan span =
            parser_make_span(&frame->start, &parser->previous);
        if (!parser_set_node_span(parser, input, &span))
            return CCFALSE;
        *completed = input;
        *done = CCTRUE;
        return CCTRUE;
    }

    if (frame->stage == MK_EXPR_STAGE_CALL ||
        frame->stage == MK_EXPR_STAGE_CALL_ARG)
    {
        if (has_input)
        {
            if (!parser_append_child(parser, frame->node, input))
                return CCFALSE;
            frame->count++;
            if (parser->current.type == MK_TOK_COMMA)
            {
                if (!parser_advance(parser))
                    return CCFALSE;
                if (parser->current.type == MK_TOK_RPAREN)
                    return parser_error_current(
                        parser, "函数调用实参不允许尾逗号"
                    ), CCFALSE;
                frame->stage = MK_EXPR_STAGE_CALL_ARG;
                MKExprFrame child;
                memset(&child, 0, sizeof(child));
                child.task = MK_EXPR_TASK_EXPR;
                child.stage = MK_EXPR_STAGE_START;
                child.start = parser->current;
                if (!parser_expr_push(parser, frames, &child))
                    return CCFALSE;
                *done = CCFALSE;
                return CCTRUE;
            }
        }
        else if (parser->current.type != MK_TOK_RPAREN)
        {
            frame->stage = MK_EXPR_STAGE_CALL_ARG;
            MKExprFrame child;
            memset(&child, 0, sizeof(child));
            child.task = MK_EXPR_TASK_EXPR;
            child.stage = MK_EXPR_STAGE_START;
            child.start = parser->current;
            if (!parser_expr_push(parser, frames, &child))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        if (!parser_expect(parser, MK_TOK_RPAREN, "右圆括号"))
            return CCFALSE;
        MKSourceSpan span =
            parser_make_span(&frame->callee_token, &parser->previous);
        if (!parser_set_node_span(parser, frame->node, &span))
            return CCFALSE;
        *completed = frame->node;
        *done = CCTRUE;
        return CCTRUE;
    }

    if (frame->stage == MK_EXPR_STAGE_LIST ||
        frame->stage == MK_EXPR_STAGE_LIST_ELEM)
    {
        if (has_input)
        {
            if (!parser_append_child(parser, frame->node, input))
                return CCFALSE;
            frame->count++;
            if (parser->current.type == MK_TOK_COMMA)
            {
                if (!parser_advance(parser))
                    return CCFALSE;
            }
            else if (parser->current.type != MK_TOK_RBRACE)
            {
                return parser_error_current(
                    parser, "列表元素之间需要逗号或右花括号"
                ), CCFALSE;
            }
        }

        if (parser->current.type == MK_TOK_RBRACE)
        {
            if (!parser_expect(parser, MK_TOK_RBRACE, "右花括号"))
                return CCFALSE;
            MKSourceSpan span =
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

        frame->stage = MK_EXPR_STAGE_LIST_ELEM;
        MKExprFrame child;
        memset(&child, 0, sizeof(child));
        child.task = MK_EXPR_TASK_EXPR;
        child.stage = MK_EXPR_STAGE_START;
        child.start = parser->current;
        if (!parser_expr_push(parser, frames, &child))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    return CCFALSE;
}

static CCBOOL parser_expr_task_primary(
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
    if (!has_input && frame->stage == MK_EXPR_STAGE_START)
        return parser_expr_primary_start(
            parser, frame, frames, has_input, input, completed, done
        );
    return parser_expr_primary_resume(
        parser, frame, frames, has_input, input, completed, done
    );
}

static CCBOOL parser_expr_step(
    MKParser *parser, MKExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKASTNodeId input,
    MKASTNodeId *completed, CCBOOL *done
)
{
    switch (frame->task)
    {
    case MK_EXPR_TASK_EXPR:
        return parser_expr_task_expr(
            parser, frame, frames, has_input, input, completed, done
        );
    case MK_EXPR_TASK_BINARY:
        return parser_expr_task_binary(
            parser, frame, frames, has_input, input, completed, done
        );
    case MK_EXPR_TASK_UNARY:
        return parser_expr_task_unary(
            parser, frame, frames, has_input, input, completed, done
        );
    case MK_EXPR_TASK_POSTFIX:
        return parser_expr_task_postfix(
            parser, frame, frames, has_input, input, completed, done
        );
    case MK_EXPR_TASK_PRIMARY:
        return parser_expr_task_primary(
            parser, frame, frames, has_input, input, completed, done
        );
    default:
        return CCFALSE;
    }
}

static MKASTNodeId parser_expr_run(MKParser *parser)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    MKASTNodeId completed = MK_AST_NODE_NIL;
    CCBOOL has_completed = CCFALSE;

    for (;;)
    {
        if (has_completed)
        {
            MKExprFrame *frame = parser_expr_top(&frames);
            if (!frame)
            {
                ccalloc(frames.array, 0, NULL);
                return completed;
            }
            MKASTNodeId result = MK_AST_NODE_NIL;
            CCBOOL done = CCFALSE;
            if (!parser_expr_step(
                    parser, frame, &frames, CCTRUE, completed,
                    &result, &done
                ))
                goto Failed;
            if (done)
            {
                parser_expr_frame_cleanup(parser, frame);
                MKExprFrame popped;
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
            MKExprFrame start;
            memset(&start, 0, sizeof(start));
            start.task = MK_EXPR_TASK_EXPR;
            start.stage = MK_EXPR_STAGE_START;
            start.start = parser->current;
            if (!parser_expr_push(parser, &frames, &start))
                goto Failed;
            continue;
        }

        MKExprFrame *frame = parser_expr_top(&frames);
        if (!frame)
            goto Failed;
        MKASTNodeId result = MK_AST_NODE_NIL;
        CCBOOL done = CCFALSE;
        if (!parser_expr_step(
                parser, frame, &frames, CCFALSE, MK_AST_NODE_NIL,
                &result, &done
            ))
            goto Failed;
        if (done)
        {
            parser_expr_frame_cleanup(parser, frame);
            MKExprFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!parser_expr_pop(&frames, &popped))
                goto Failed;
            completed = result;
            has_completed = CCTRUE;
        }
    }

Failed:
    parser_expr_frames_abort(parser, &frames);
    return MK_AST_NODE_NIL;
}


MKASTNodeId parser_parse_expression(MKParser *parser)
{
    if (!parser || parser->failed)
        return MK_AST_NODE_NIL;
    return parser_expr_run(parser);
}
