/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK Parser 基础设施、诊断、预算与事务式入口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_parser_internal.h"

#include <stdarg.h>
#include <string.h>

/* ============================================================================
 * 诊断
 * ========================================================================== */

CCBOOL parser_error_at(
    MKParser *parser, CCUINT32 line, CCUINT32 col,
    const char *fmt, ...
)
{
    if (!parser)
        return CCFALSE;

    va_list ap;
    va_start(ap, fmt);
    const char *file = parser->lexer ? parser->lexer->file : NULL;
    (void)mk_diagnostic_vset(
        &parser->diagnostic, MK_DIAG_ERROR,
        file, line, col, fmt, ap
    );
    va_end(ap);
    parser->failed = CCTRUE;
    return CCFALSE;
}

CCBOOL parser_error_current(MKParser *parser, const char *fmt, ...)
{
    if (!parser)
        return CCFALSE;

    va_list ap;
    va_start(ap, fmt);
    const char *file = parser->lexer ? parser->lexer->file : NULL;
    (void)mk_diagnostic_vset(
        &parser->diagnostic, MK_DIAG_ERROR,
        file, parser->current.line, parser->current.col, fmt, ap
    );
    va_end(ap);
    parser->failed = CCTRUE;
    return CCFALSE;
}

CCBOOL parser_copy_lexer_diagnostic(MKParser *parser)
{
    if (!parser)
        return CCFALSE;

    const MKDiagnostic *diag = mk_lexer_diagnostic(parser->lexer);
    if (!diag || !diag->message)
        return parser_error_at(parser, 0, 0, "词法错误");

    return parser_error_at(
        parser, diag->line, diag->col,
        "词法错误：%s", diag->message
    );
}

/* ============================================================================
 * Token 读取
 * ========================================================================== */

CCBOOL parser_fetch(MKParser *parser, MKToken *out)
{
    if (!parser || !out)
        return CCFALSE;
    if (parser->failed)
        return CCFALSE;
    if (!parser->lexer)
        return parser_error_at(parser, 0, 0, "Lexer 未初始化");

    if (parser->limits.max_tokens != 0 &&
        parser->token_count >= parser->limits.max_tokens)
        return parser_error_at(
            parser, parser->lexer->line, parser->lexer->col,
            "Token 数量超过限制"
        );

    if (!mk_lexer_next(parser->lexer, out))
        return parser_copy_lexer_diagnostic(parser);

    parser->token_count++;
    return CCTRUE;
}

CCBOOL parser_advance(MKParser *parser)
{
    if (!parser || parser->failed)
        return CCFALSE;

    parser->previous = parser->current;
    if (parser->has_lookahead)
    {
        parser->current = parser->lookahead;
        parser->has_lookahead = CCFALSE;
        return CCTRUE;
    }
    return parser_fetch(parser, &parser->current);
}

CCBOOL parser_peek(MKParser *parser, MKToken *out)
{
    if (!parser || parser->failed)
        return CCFALSE;
    if (parser->current.type == MK_TOK_STRING)
        return parser_error_current(
            parser, "内部错误：字符串 Token 后不允许前瞻"
        );

    if (!parser->has_lookahead)
    {
        if (!parser_fetch(parser, &parser->lookahead))
            return CCFALSE;
        parser->has_lookahead = CCTRUE;
    }

    if (out)
        *out = parser->lookahead;
    return CCTRUE;
}

CCBOOL parser_match(MKParser *parser, MKTokenType type)
{
    if (!parser || parser->failed)
        return CCFALSE;
    if (parser->current.type != type)
        return CCFALSE;
    return parser_advance(parser);
}

CCBOOL parser_expect(
    MKParser *parser, MKTokenType type, const char *what
)
{
    if (!parser || parser->failed)
        return CCFALSE;
    if (parser->current.type != type)
        return parser_error_current(
            parser, "期望%s，实际%s",
            what ? what : mk_token_type_name(type),
            mk_token_type_name(parser->current.type)
        );
    return parser_advance(parser);
}

/* ============================================================================
 * 预算
 * ========================================================================== */

CCBOOL parser_enter_syntax(MKParser *parser)
{
    if (!parser || parser->failed)
        return CCFALSE;
    if (parser->limits.max_syntax_depth != 0 &&
        parser->syntax_depth >= parser->limits.max_syntax_depth)
        return parser_error_current(parser, "语法嵌套深度超过限制");
    parser->syntax_depth++;
    return CCTRUE;
}

void parser_leave_syntax(MKParser *parser)
{
    if (parser && parser->syntax_depth > 0)
        parser->syntax_depth--;
}

CCBOOL parser_enter_expression(MKParser *parser)
{
    if (!parser || parser->failed)
        return CCFALSE;
    if (parser->limits.max_expression_depth != 0 &&
        parser->expression_depth >= parser->limits.max_expression_depth)
        return parser_error_current(parser, "表达式嵌套深度超过限制");
    parser->expression_depth++;
    return CCTRUE;
}

void parser_leave_expression(MKParser *parser)
{
    if (parser && parser->expression_depth > 0)
        parser->expression_depth--;
}

CCBOOL parser_count_declaration(MKParser *parser)
{
    if (!parser || parser->failed)
        return CCFALSE;
    if (parser->limits.max_declarations != 0 &&
        parser->declaration_count >= parser->limits.max_declarations)
        return parser_error_current(
            parser, "模块顶层声明数量超过限制"
        );
    parser->declaration_count++;
    return CCTRUE;
}

/* ============================================================================
 * AST 构建
 * ========================================================================== */

MKASTNodeId parser_new_node(
    MKParser *parser, MKASTKind kind, const MKSourceSpan *span
)
{
    if (!parser || !parser->ast)
        return MK_AST_NODE_NIL;

    if (parser->limits.max_ast_nodes != 0 &&
        mk_ast_node_count(parser->ast) >= parser->limits.max_ast_nodes)
    {
        (void)parser_error_current(parser, "AST 节点数量超过限制");
        return MK_AST_NODE_NIL;
    }

    MKASTNodeId id = mk_ast_add_node(parser->ast, kind, span);
    if (id == MK_AST_NODE_NIL)
    {
        (void)parser_error_current(parser, "AST 节点分配失败");
        return MK_AST_NODE_NIL;
    }
    return id;
}

MKASTStringId parser_new_string(
    MKParser *parser, const char *data, CCUINT32 len
)
{
    if (!parser || !parser->ast)
        return MK_AST_STRING_NIL;

    MKASTStringId id = mk_ast_add_string(parser->ast, data, len);
    if (id == MK_AST_STRING_NIL)
    {
        (void)parser_error_current(parser, "AST 字符串分配失败");
        return MK_AST_STRING_NIL;
    }
    return id;
}

CCBOOL parser_append_child(
    MKParser *parser, MKASTNodeId parent, MKASTNodeId child
)
{
    if (!parser || !parser->ast)
        return CCFALSE;
    if (!mk_ast_append_child(parser->ast, parent, child))
        return parser_error_current(parser, "AST 子节点挂接失败");
    return CCTRUE;
}

CCBOOL parser_set_node_span(
    MKParser *parser, MKASTNodeId id, const MKSourceSpan *span
)
{
    if (!parser || !parser->ast || !span)
        return CCFALSE;
    MKASTNode *node = mk_ast_node_mut(parser->ast, id);
    if (!node)
        return parser_error_current(parser, "AST 节点缺失");
    node->span = *span;
    return CCTRUE;
}

CCBOOL parser_take_identifier(
    MKParser *parser, MKASTStringId *out
)
{
    if (!parser || !out || parser->failed)
        return CCFALSE;
    if (parser->current.type != MK_TOK_IDENT)
        return parser_error_current(
            parser, "期望标识符，实际%s",
            mk_token_type_name(parser->current.type)
        );

    MKASTStringId id = parser_new_string(
        parser, parser->current.text, parser->current.len
    );
    if (id == MK_AST_STRING_NIL)
        return CCFALSE;

    *out = id;
    return parser_advance(parser);
}

CCBOOL parser_take_string(
    MKParser *parser, MKASTStringId *out
)
{
    if (!parser || !out || parser->failed)
        return CCFALSE;
    if (parser->current.type != MK_TOK_STRING)
        return parser_error_current(
            parser, "期望字符串，实际%s",
            mk_token_type_name(parser->current.type)
        );

    MKASTStringId id = parser_new_string(
        parser, parser->current.string, parser->current.string_len
    );
    if (id == MK_AST_STRING_NIL)
        return CCFALSE;

    *out = id;
    return parser_advance(parser);
}

/* ============================================================================
 * 源码范围
 * ========================================================================== */

MKSourceSpan parser_token_span(const MKToken *token)
{
    MKSourceSpan span;
    memset(&span, 0, sizeof(span));
    if (!token)
        return span;

    span.offset = token->offset;
    span.width = token->width;
    span.line = token->line;
    span.col = token->col;
    span.end_line = token->line;
    span.end_col = token->col;
    if (token->width > 0)
    {
        CCUINT64 end_col = (CCUINT64)token->col + token->width - 1;
        if (end_col > (CCUINT64)0xFFFFFFFFu)
            end_col = (CCUINT64)0xFFFFFFFFu;
        span.end_col = (CCUINT32)end_col;
    }
    return span;
}

MKSourceSpan parser_make_span(
    const MKToken *start, const MKToken *end
)
{
    MKSourceSpan span;
    memset(&span, 0, sizeof(span));
    if (!start || !end)
        return span;

    span.offset = start->offset;
    span.line = start->line;
    span.col = start->col;

    CCUINT64 end_offset = end->offset + end->width;
    if (end_offset >= start->offset)
        span.width = end_offset - start->offset;

    span.end_line = end->line;
    span.end_col = end->col;
    if (end->width > 0)
    {
        CCUINT64 end_col = (CCUINT64)end->col + end->width - 1;
        if (end_col > (CCUINT64)0xFFFFFFFFu)
            end_col = (CCUINT64)0xFFFFFFFFu;
        span.end_col = (CCUINT32)end_col;
    }
    return span;
}

/* ============================================================================
 * 生命周期与事务
 * ========================================================================== */

CCBOOL mk_parser_init(
    MKParser *parser, MKLexer *lexer, const MKLimits *limits
)
{
    if (!parser)
        return CCFALSE;

    memset(parser, 0, sizeof(*parser));
    mk_diagnostic_init(&parser->diagnostic);
    parser->lexer = lexer;
    if (limits)
        parser->limits = *limits;
    else
        mk_limits_init(&parser->limits);

    if (!lexer)
        return parser_error_at(
            parser, 0, 0, "Parser 初始化参数非法：Lexer 为空"
        );

    if (!parser_fetch(parser, &parser->current))
        return CCFALSE;

    return CCTRUE;
}

void mk_parser_clear(MKParser *parser)
{
    if (!parser)
        return;
    mk_diagnostic_clear(&parser->diagnostic);
    memset(parser, 0, sizeof(*parser));
}

CCBOOL mk_parser_parse(MKParser *parser, MKAST **out_ast)
{
    if (!out_ast)
        return CCFALSE;
    *out_ast = NULL;
    if (!parser)
        return CCFALSE;
    if (parser->failed || parser->used || !parser->lexer)
        return CCFALSE;
    parser->used = CCTRUE;

    MKAST *ast = mk_ast_create(parser->lexer->file);
    if (!ast)
    {
        (void)parser_error_at(
            parser, parser->current.line, parser->current.col,
            "AST 创建失败"
        );
        return CCFALSE;
    }
    parser->ast = ast;

    if (!parser_parse_module(parser))
    {
        parser->ast = NULL;
        mk_ast_destroy(ast);
        return CCFALSE;
    }

    const char *reason = NULL;
    if (!mk_ast_validate(ast, &reason))
    {
        parser->ast = NULL;
        mk_ast_destroy(ast);
        (void)parser_error_at(
            parser, parser->current.line, parser->current.col,
            "AST 内部校验失败：%s", reason ? reason : "未知原因"
        );
        return CCFALSE;
    }

    parser->ast = NULL;
    *out_ast = ast;
    return CCTRUE;
}

const MKDiagnostic *mk_parser_diagnostic(const MKParser *parser)
{
    if (!parser)
        return NULL;
    return &parser->diagnostic;
}
