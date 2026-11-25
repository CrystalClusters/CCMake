/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK Parser 内部辅助接口（不对外公开）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_PARSER_INTERNAL_H_
#define _INCLUDE_MK_PARSER_INTERNAL_H_

#include "mk_parser.h"

/* Token 读取与消费 */
CCBOOL parser_fetch(MKParser *parser, MKToken *out);
CCBOOL parser_advance(MKParser *parser);
CCBOOL parser_peek(MKParser *parser, MKToken *out);
CCBOOL parser_match(MKParser *parser, MKTokenType type);
CCBOOL parser_expect(
    MKParser *parser, MKTokenType type, const char *what
);

/* 诊断 */
CCBOOL parser_error_at(
    MKParser *parser, CCUINT32 line, CCUINT32 col,
    const char *fmt, ...
);
CCBOOL parser_error_current(MKParser *parser, const char *fmt, ...);
CCBOOL parser_copy_lexer_diagnostic(MKParser *parser);

/* 预算 */
CCBOOL parser_enter_syntax(MKParser *parser);
void parser_leave_syntax(MKParser *parser);
CCBOOL parser_enter_expression(MKParser *parser);
void parser_leave_expression(MKParser *parser);
CCBOOL parser_count_declaration(MKParser *parser);

/* AST 构建 */
MKASTNodeId parser_new_node(
    MKParser *parser, MKASTKind kind, const MKSourceSpan *span
);
MKASTStringId parser_new_string(
    MKParser *parser, const char *data, CCUINT32 len
);
CCBOOL parser_append_child(
    MKParser *parser, MKASTNodeId parent, MKASTNodeId child
);
CCBOOL parser_set_node_span(
    MKParser *parser, MKASTNodeId id, const MKSourceSpan *span
);

/* Token 文本复制 */
CCBOOL parser_take_identifier(
    MKParser *parser, MKASTStringId *out
);
CCBOOL parser_take_string(
    MKParser *parser, MKASTStringId *out
);

/* 语法入口（跨文件） */
CCBOOL parser_parse_module(MKParser *parser);
CCBOOL parser_is_type_start(MKParser *parser);
MKASTNodeId parser_parse_expression(MKParser *parser);
MKASTNodeId parser_parse_type(MKParser *parser);
MKASTNodeId parser_parse_return_type(MKParser *parser);
MKASTNodeId parser_parse_var_decl(MKParser *parser);
MKASTNodeId parser_parse_statement(MKParser *parser);
MKASTNodeId parser_parse_block(MKParser *parser);

/* 源码范围 */
MKSourceSpan parser_token_span(const MKToken *token);
MKSourceSpan parser_make_span(
    const MKToken *start, const MKToken *end
);

#endif
