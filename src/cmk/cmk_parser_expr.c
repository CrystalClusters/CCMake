/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 表达式 Parser：Pratt、primary、postfix、list 与字面量
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_parser_internal.h"

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

static CMKASTNodeId parser_parse_list(CMKParser *parser)
{
    CMKToken start = parser->current;
    if (!parser_expect(parser, CMK_TOK_LBRACE, "左花括号"))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId list = parser_new_node(parser, CMK_AST_LIST, NULL);
    if (list == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CCUINT32 count = 0;
    if (parser->current.type != CMK_TOK_RBRACE)
    {
        for (;;)
        {
            if (parser->limits.max_list_elements != 0 &&
                count >= parser->limits.max_list_elements)
                return parser_error_current(
                    parser, "列表元素数量超过限制"
                ), CMK_AST_NODE_NIL;

            CMKASTNodeId element = parser_parse_expression(parser);
            if (element == CMK_AST_NODE_NIL)
                return CMK_AST_NODE_NIL;
            if (!parser_append_child(parser, list, element))
                return CMK_AST_NODE_NIL;
            count++;

            if (parser->current.type == CMK_TOK_COMMA)
            {
                if (!parser_advance(parser))
                    return CMK_AST_NODE_NIL;
                if (parser->current.type == CMK_TOK_RBRACE)
                    break;
                continue;
            }
            if (parser->current.type == CMK_TOK_RBRACE)
                break;
            return parser_error_current(
                parser, "列表元素之间需要逗号或右花括号"
            ), CMK_AST_NODE_NIL;
        }
    }

    if (!parser_expect(parser, CMK_TOK_RBRACE, "右花括号"))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    if (!parser_set_node_span(parser, list, &span))
        return CMK_AST_NODE_NIL;
    return list;
}

static CCBOOL parser_parse_call_suffix(
    CMKParser *parser, CMKASTNodeId call
)
{
    if (!parser_expect(parser, CMK_TOK_LPAREN, "左圆括号"))
        return CCFALSE;
    if (parser->current.type != CMK_TOK_RPAREN)
    {
        for (;;)
        {
            CMKASTNodeId arg = parser_parse_expression(parser);
            if (arg == CMK_AST_NODE_NIL)
                return CCFALSE;
            if (!parser_append_child(parser, call, arg))
                return CCFALSE;

            if (parser->current.type == CMK_TOK_COMMA)
            {
                if (!parser_advance(parser))
                    return CCFALSE;
                if (parser->current.type == CMK_TOK_RPAREN)
                    return parser_error_current(
                        parser, "函数调用实参不允许尾逗号"
                    );
                continue;
            }
            break;
        }
    }
    return parser_expect(parser, CMK_TOK_RPAREN, "右圆括号");
}

static CMKASTNodeId parser_parse_ident_or_call(CMKParser *parser)
{
    CMKToken token = parser->current;
    CMKASTStringId name = parser_new_string(
        parser, token.text, token.len
    );
    if (name == CMK_AST_STRING_NIL)
        return CMK_AST_NODE_NIL;

    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;

    if (parser->current.type != CMK_TOK_LPAREN)
    {
        CMKSourceSpan span = parser_token_span(&token);
        CMKASTNodeId id = parser_new_node(
            parser, CMK_AST_IDENT, &span
        );
        if (id == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
        CMKASTNode *node = cmk_ast_node_mut(parser->ast, id);
        if (!node)
            return CMK_AST_NODE_NIL;
        node->as.ident.name = name;
        return id;
    }

    CMKASTNodeId call = parser_new_node(parser, CMK_AST_CALL, NULL);
    if (call == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *node = cmk_ast_node_mut(parser->ast, call);
    if (!node)
        return CMK_AST_NODE_NIL;
    node->as.call.callee = name;

    if (!parser_parse_call_suffix(parser, call))
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&token, &parser->previous);
    if (!parser_set_node_span(parser, call, &span))
        return CMK_AST_NODE_NIL;
    return call;
}

static CMKASTNodeId parser_parse_primary(CMKParser *parser)
{
    switch (parser->current.type)
    {
    case CMK_TOK_INT:
    case CMK_TOK_FLOAT:
    case CMK_TOK_STRING:
    case CMK_TOK_KW_TRUE:
    case CMK_TOK_KW_FALSE:
        return parser_parse_literal(parser);

    case CMK_TOK_CONFIG_REF:
        return parser_parse_config_ref(parser);

    case CMK_TOK_IDENT:
        return parser_parse_ident_or_call(parser);

    case CMK_TOK_LPAREN:
    {
        CMKToken start = parser->current;
        if (!parser_advance(parser))
            return CMK_AST_NODE_NIL;
        CMKASTNodeId expression = parser_parse_expression(parser);
        if (expression == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
        if (!parser_expect(parser, CMK_TOK_RPAREN, "右圆括号"))
            return CMK_AST_NODE_NIL;

        CMKSourceSpan span = parser_make_span(&start, &parser->previous);
        if (!parser_set_node_span(parser, expression, &span))
            return CMK_AST_NODE_NIL;
        return expression;
    }

    case CMK_TOK_LBRACE:
        return parser_parse_list(parser);

    case CMK_TOK_UNDERSCORE:
        return parser_error_current(
            parser, "`_` 只能作为赋值目标"
        ), CMK_AST_NODE_NIL;

    default:
        return parser_error_current(
            parser, "期望表达式，实际%s",
            cmk_token_type_name(parser->current.type)
        );
    }
}

static CMKASTNodeId parser_parse_postfix(CMKParser *parser)
{
    CMKToken start = parser->current;
    CMKASTNodeId base = parser_parse_primary(parser);
    if (base == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    while (parser->current.type == CMK_TOK_LBRACKET ||
           parser->current.type == CMK_TOK_DOT)
    {
        if (parser->current.type == CMK_TOK_LBRACKET)
        {
            if (!parser_advance(parser))
                return CMK_AST_NODE_NIL;
            CMKASTNodeId index = parser_parse_expression(parser);
            if (index == CMK_AST_NODE_NIL)
                return CMK_AST_NODE_NIL;
            if (!parser_expect(parser, CMK_TOK_RBRACKET, "右方括号"))
                return CMK_AST_NODE_NIL;

            CMKSourceSpan span =
                parser_make_span(&start, &parser->previous);
            CMKASTNodeId node = parser_new_node(
                parser, CMK_AST_INDEX, &span
            );
            if (node == CMK_AST_NODE_NIL)
                return CMK_AST_NODE_NIL;
            if (!parser_append_child(parser, node, base) ||
                !parser_append_child(parser, node, index))
                return CMK_AST_NODE_NIL;
            base = node;
            continue;
        }

        if (!parser_advance(parser))
            return CMK_AST_NODE_NIL;
        if (parser->current.type != CMK_TOK_IDENT)
            return parser_error_current(
                parser, "成员访问需要标识符"
            ), CMK_AST_NODE_NIL;

        CMKASTStringId member = parser_new_string(
            parser, parser->current.text, parser->current.len
        );
        if (member == CMK_AST_STRING_NIL)
            return CMK_AST_NODE_NIL;
        if (!parser_advance(parser))
            return CMK_AST_NODE_NIL;

        CMKSourceSpan span = parser_make_span(&start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_MEMBER, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
        CMKASTNode *member_node = cmk_ast_node_mut(parser->ast, node);
        if (!member_node)
            return CMK_AST_NODE_NIL;
        member_node->as.member.member = member;
        if (!parser_append_child(parser, node, base))
            return CMK_AST_NODE_NIL;
        base = node;
    }

    return base;
}

static CMKASTNodeId parser_parse_unary(CMKParser *parser)
{
    CMKTokenType type = parser->current.type;
    if (type != CMK_TOK_NOT && type != CMK_TOK_MINUS &&
        type != CMK_TOK_PLUS)
        return parser_parse_postfix(parser);

    CMKToken start = parser->current;
    CMKUnaryOp op = (type == CMK_TOK_NOT) ? CMK_UNARY_NOT
        : (type == CMK_TOK_MINUS) ? CMK_UNARY_NEG : CMK_UNARY_POS;
    if (!parser_advance(parser))
        return CMK_AST_NODE_NIL;

    // -9223372036854775808 直接构造 INT64_MIN，避免正向字面量越界
    if (op == CMK_UNARY_NEG && parser->current.type == CMK_TOK_INT)
    {
        CCUINT64 magnitude = 0;
        if (!parser_int_magnitude(
                parser, &parser->current,
                (CCUINT64)0x8000000000000000u, &magnitude
            ))
            return CMK_AST_NODE_NIL;

        if (magnitude == (CCUINT64)0x8000000000000000u)
        {
            CMKToken int_token = parser->current;
            CMKSourceSpan span = parser_make_span(&start, &int_token);
            CMKASTNodeId node = parser_new_node(
                parser, CMK_AST_LITERAL, &span
            );
            if (node == CMK_AST_NODE_NIL)
                return CMK_AST_NODE_NIL;
            CMKASTNode *literal = cmk_ast_node_mut(parser->ast, node);
            if (!literal)
                return CMK_AST_NODE_NIL;
            literal->as.literal.kind = CMK_LITERAL_INT;
            literal->as.literal.value.integer =
                -((CCINT64)0x7FFFFFFFFFFFFFFF) - 1;
            if (!parser_advance(parser))
                return CMK_AST_NODE_NIL;
            return node;
        }
    }

    if (!parser_enter_expression(parser))
        return CMK_AST_NODE_NIL;
    CMKASTNodeId operand = parser_parse_unary(parser);
    parser_leave_expression(parser);
    if (operand == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CMKSourceSpan span = parser_make_span(&start, &parser->previous);
    CMKASTNodeId node = parser_new_node(parser, CMK_AST_UNARY, &span);
    if (node == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;
    CMKASTNode *unary = cmk_ast_node_mut(parser->ast, node);
    if (!unary)
        return CMK_AST_NODE_NIL;
    unary->as.unary.op = op;
    if (!parser_append_child(parser, node, operand))
        return CMK_AST_NODE_NIL;
    return node;
}

static CMKASTNodeId parser_parse_binary(
    CMKParser *parser, CCUINT32 min_bp
)
{
    CMKToken start = parser->current;
    CMKASTNodeId left = parser_parse_unary(parser);
    if (left == CMK_AST_NODE_NIL)
        return CMK_AST_NODE_NIL;

    CCBOOL relational_seen = CCFALSE;
    for (;;)
    {
        CMKBinaryOp op;
        CCUINT32 lbp = 0;
        CCBOOL relational = CCFALSE;
        if (!parser_binary_operator(
                parser->current.type, &op, &lbp, &relational
            ))
            break;
        if (lbp < min_bp)
            break;
        if (relational && relational_seen)
            return parser_error_current(
                parser, "不支持链式比较"
            ), CMK_AST_NODE_NIL;
        if (relational)
            relational_seen = CCTRUE;

        if (!parser_advance(parser))
            return CMK_AST_NODE_NIL;
        CMKASTNodeId right = parser_parse_binary(parser, lbp + 1);
        if (right == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;

        CMKSourceSpan span = parser_make_span(&start, &parser->previous);
        CMKASTNodeId node = parser_new_node(
            parser, CMK_AST_BINARY, &span
        );
        if (node == CMK_AST_NODE_NIL)
            return CMK_AST_NODE_NIL;
        CMKASTNode *binary = cmk_ast_node_mut(parser->ast, node);
        if (!binary)
            return CMK_AST_NODE_NIL;
        binary->as.binary.op = op;
        if (!parser_append_child(parser, node, left) ||
            !parser_append_child(parser, node, right))
            return CMK_AST_NODE_NIL;
        left = node;
    }

    return left;
}

CMKASTNodeId parser_parse_expression(CMKParser *parser)
{
    if (!parser || parser->failed)
        return CMK_AST_NODE_NIL;
    if (!parser_enter_expression(parser))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId expression = parser_parse_binary(parser, 0);
    parser_leave_expression(parser);
    return expression;
}
