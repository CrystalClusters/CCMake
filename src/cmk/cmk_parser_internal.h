/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK Parser 内部辅助接口（不对外公开）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_PARSER_INTERNAL_H_
#define _INCLUDE_CMK_PARSER_INTERNAL_H_

#include "cmk_parser.h"

/* Token 读取与消费 */
CCBOOL parser_fetch(CMKParser *parser, CMKToken *out);
CCBOOL parser_advance(CMKParser *parser);
CCBOOL parser_peek(CMKParser *parser, CMKToken *out);
CCBOOL parser_match(CMKParser *parser, CMKTokenType type);
CCBOOL parser_expect(
    CMKParser *parser, CMKTokenType type, const char *what
);

/* 诊断 */
CCBOOL parser_error_at(
    CMKParser *parser, CCUINT32 line, CCUINT32 col,
    const char *fmt, ...
);
CCBOOL parser_error_current(CMKParser *parser, const char *fmt, ...);
CCBOOL parser_copy_lexer_diagnostic(CMKParser *parser);

/* 预算 */
CCBOOL parser_enter_syntax(CMKParser *parser);
void parser_leave_syntax(CMKParser *parser);
CCBOOL parser_enter_expression(CMKParser *parser);
void parser_leave_expression(CMKParser *parser);
CCBOOL parser_count_declaration(CMKParser *parser);

/* AST 构建 */
CMKASTNodeId parser_new_node(
    CMKParser *parser, CMKASTKind kind, const CMKSourceSpan *span
);
CMKASTStringId parser_new_string(
    CMKParser *parser, const char *data, CCUINT32 len
);
CCBOOL parser_append_child(
    CMKParser *parser, CMKASTNodeId parent, CMKASTNodeId child
);
CCBOOL parser_set_node_span(
    CMKParser *parser, CMKASTNodeId id, const CMKSourceSpan *span
);

/* Token 文本复制 */
CCBOOL parser_take_identifier(
    CMKParser *parser, CMKASTStringId *out
);
CCBOOL parser_take_string(
    CMKParser *parser, CMKASTStringId *out
);

/* 语法入口（跨文件） */
CCBOOL parser_parse_module(CMKParser *parser);
CCBOOL parser_is_type_start(CMKParser *parser);
CMKASTNodeId parser_parse_expression(CMKParser *parser);
CMKASTNodeId parser_parse_type(CMKParser *parser);
CMKASTNodeId parser_parse_return_type(CMKParser *parser);
CMKASTNodeId parser_parse_var_decl(CMKParser *parser);
CMKASTNodeId parser_parse_statement(CMKParser *parser);
CMKASTNodeId parser_parse_block(CMKParser *parser);

/* 源码范围 */
CMKSourceSpan parser_token_span(const CMKToken *token);
CMKSourceSpan parser_make_span(
    const CMKToken *start, const CMKToken *end
);

#endif
