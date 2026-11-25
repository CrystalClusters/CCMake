/*
 * @Description: MK Parser 基础设施测试：Token 缓冲、诊断、预算与事务入口。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "mk/mk_parser.h"
#include "mk/mk_parser_internal.h"

#include <stdio.h>
#include <string.h>

static CCBOOL parser_begin(
    MKParser *parser,
    MKLexer *lexer,
    const char *src,
    const MKLimits *limits
)
{
    if (!mk_lexer_init(
            lexer, "test.mk", src, (CCUINT64)strlen(src)
        ))
        return CCFALSE;
    return mk_parser_init(parser, lexer, limits);
}

void test_mk_parser_base(void)
{
    /* ------------------------------------------------------------------
     * PARSER-001 空文件与事务发布
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-001", "空文件与事务发布");
    {
        MKLexer lexer;
        MKParser parser;
        MKAST *ast = NULL;
        CC_CHECK(
            parser_begin(&parser, &lexer, "", NULL),
            "初始化空文件解析"
        );
        CC_CHECK(mk_parser_parse(&parser, &ast), "解析空文件");
        CC_CHECK(ast != NULL, "发布空模块 AST");

        if (ast)
        {
            const char *reason = NULL;
            CC_CHECK(mk_ast_validate(ast, &reason), "空模块校验通过");
            CC_CHECK_EQ_I64(
                "空模块节点数", 1, (CCINT64)mk_ast_node_count(ast)
            );
            const MKASTNode *root = mk_ast_node(ast, mk_ast_root(ast));
            CC_CHECK(root != NULL, "读取空模块根节点");
            if (root)
            {
                CC_CHECK_EQ_I64(
                    "根节点类型", (CCINT64)MK_AST_MODULE,
                    (CCINT64)root->kind
                );
                CC_CHECK_EQ_I64("span offset", 0, (CCINT64)root->span.offset);
                CC_CHECK_EQ_I64("span width", 0, (CCINT64)root->span.width);
                CC_CHECK_EQ_I64("span line", 1, (CCINT64)root->span.line);
                CC_CHECK_EQ_I64("span col", 1, (CCINT64)root->span.col);
            }
            mk_ast_destroy(ast);
        }

        ast = (MKAST *)1;
        CC_CHECK(!mk_parser_parse(&parser, &ast), "Parser 只能解析一次");
        CC_CHECK(ast == NULL, "失败时清空 out_ast");

        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-002 注释后的 EOF 位置
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-002", "注释后的 EOF 位置");
    {
        MKLexer lexer;
        MKParser parser;
        MKAST *ast = NULL;
        CC_CHECK(
            parser_begin(&parser, &lexer, "// only\n", NULL),
            "初始化行注释文件"
        );
        CC_CHECK(mk_parser_parse(&parser, &ast), "解析行注释文件");
        if (ast)
        {
            const MKASTNode *root =
                mk_ast_node(ast, mk_ast_root(ast));
            CC_CHECK(root != NULL, "读取根节点");
            if (root)
            {
                CC_CHECK_EQ_I64("offset", 8, (CCINT64)root->span.offset);
                CC_CHECK_EQ_I64("width", 0, (CCINT64)root->span.width);
                CC_CHECK_EQ_I64("line", 2, (CCINT64)root->span.line);
                CC_CHECK_EQ_I64("col", 1, (CCINT64)root->span.col);
            }
            mk_ast_destroy(ast);
        }
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);

        CC_CHECK(
            parser_begin(&parser, &lexer, "/* x */", NULL),
            "初始化块注释文件"
        );
        ast = NULL;
        CC_CHECK(mk_parser_parse(&parser, &ast), "解析块注释文件");
        if (ast)
        {
            const MKASTNode *root =
                mk_ast_node(ast, mk_ast_root(ast));
            CC_CHECK(root != NULL, "读取根节点");
            if (root)
            {
                CC_CHECK_EQ_I64("offset", 7, (CCINT64)root->span.offset);
                CC_CHECK_EQ_I64("col", 8, (CCINT64)root->span.col);
            }
            mk_ast_destroy(ast);
        }
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-003 非法顶层 Token
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-003", "非法顶层 Token");
    {
        MKLexer lexer;
        MKParser parser;
        MKAST *ast = NULL;
        CC_CHECK(
            parser_begin(&parser, &lexer, ";", NULL),
            "初始化非法顶层输入"
        );
        CC_CHECK(!mk_parser_parse(&parser, &ast), "解析应失败");
        CC_CHECK(ast == NULL, "失败时不发布 AST");

        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        CC_CHECK(diag != NULL && mk_diagnostic_has(diag), "Parser 诊断");
        if (diag && diag->message)
        {
            CC_CHECK(
                strstr(diag->message, "期望顶层声明") != NULL,
                "诊断说明顶层 Token 非法"
            );
            CC_CHECK_EQ_STR("诊断文件", "test.mk", diag->file);
            CC_CHECK_EQ_I64("诊断行", 1, (CCINT64)diag->line);
            CC_CHECK_EQ_I64("诊断列", 1, (CCINT64)diag->col);
        }
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-004 词法诊断复制
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-004", "词法诊断复制");
    {
        MKLexer lexer;
        MKParser parser;
        MKAST *ast = NULL;
        CC_CHECK(
            !parser_begin(&parser, &lexer, "#", NULL),
            "词法错误导致初始化失败"
        );

        const MKDiagnostic *parser_diag = mk_parser_diagnostic(&parser);
        CC_CHECK(
            parser_diag && parser_diag->message &&
            strstr(parser_diag->message, "词法错误") != NULL,
            "Parser 诊断带词法前缀"
        );
        CC_CHECK(
            parser_diag && parser_diag->message &&
            strstr(parser_diag->message, "无法识别") != NULL,
            "Parser 诊断复制词法消息"
        );

        CC_CHECK(!mk_parser_parse(&parser, &ast), "失败后不可解析");
        CC_CHECK(ast == NULL, "失败不发布 AST");

        const MKDiagnostic *lexer_diag = mk_lexer_diagnostic(&lexer);
        CC_CHECK(
            lexer_diag && lexer_diag->message &&
            strstr(lexer_diag->message, "无法识别") != NULL,
            "Lexer 诊断未被 Parser 修改"
        );

        mk_parser_clear(&parser);
        lexer_diag = mk_lexer_diagnostic(&lexer);
        CC_CHECK(
            lexer_diag && lexer_diag->message &&
            strstr(lexer_diag->message, "无法识别") != NULL,
            "清理 Parser 后 Lexer 诊断仍存在"
        );
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-005 初始化参数与空指针
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-005", "初始化参数与空指针");
    {
        MKLexer lexer;
        memset(&lexer, 0, sizeof(lexer));
        MKParser parser;
        MKAST *ast = NULL;

        CC_CHECK(
            !mk_parser_init(NULL, &lexer, NULL),
            "空 Parser 初始化失败"
        );
        CC_CHECK(
            !mk_parser_init(&parser, NULL, NULL),
            "空 Lexer 初始化失败"
        );
        CC_CHECK(
            !mk_parser_parse(&parser, &ast),
            "空 Lexer Parser 不可解析"
        );
        CC_CHECK(ast == NULL, "空 Lexer 不发布 AST");
        mk_parser_clear(&parser);

        ast = (MKAST *)1;
        CC_CHECK(!mk_parser_parse(NULL, &ast), "空 Parser 解析失败");
        CC_CHECK(ast == NULL, "空 Parser 清空 out_ast");

        CC_CHECK(
            !mk_parser_parse(&parser, NULL),
            "空 out_ast 解析失败"
        );
        CC_CHECK(mk_parser_diagnostic(NULL) == NULL, "空 Parser 无诊断");
        mk_parser_clear(NULL);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-006 Token 预算
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-006", "Token 预算");
    {
        MKLexer lexer;
        MKParser parser;
        MKLimits limits;
        mk_limits_init(&limits);
        limits.max_tokens = 1;

        CC_CHECK(
            parser_begin(&parser, &lexer, "a b", &limits),
            "初始化 Token 预算"
        );
        CC_CHECK(!parser_advance(&parser), "超过 Token 预算应失败");
        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "Token 数量超过限制") != NULL,
            "Token 预算诊断"
        );
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);

        mk_limits_init(&limits);
        limits.max_tokens = 2;
        CC_CHECK(
            parser_begin(&parser, &lexer, "a b", &limits),
            "初始化宽松 Token 预算"
        );
        CC_CHECK(parser_advance(&parser), "第二次取 Token 成功");
        CC_CHECK(!parser_advance(&parser), "第三次取 Token 失败");
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-007 深度预算
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-007", "深度预算");
    {
        MKLexer lexer;
        MKParser parser;
        MKLimits limits;
        mk_limits_init(&limits);
        limits.max_syntax_depth = 1;

        CC_CHECK(
            parser_begin(&parser, &lexer, "", &limits),
            "初始化语法深度预算"
        );
        CC_CHECK(parser_enter_syntax(&parser), "进入第一层语法");
        CC_CHECK(!parser_enter_syntax(&parser), "超过语法深度失败");
        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "语法嵌套深度超过限制") != NULL,
            "语法深度诊断"
        );
        parser_leave_syntax(&parser);
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);

        mk_limits_init(&limits);
        limits.max_expression_depth = 1;
        CC_CHECK(
            parser_begin(&parser, &lexer, "", &limits),
            "初始化表达式深度预算"
        );
        CC_CHECK(parser_enter_expression(&parser), "进入第一层表达式");
        CC_CHECK(!parser_enter_expression(&parser), "超过表达式深度失败");
        diag = mk_parser_diagnostic(&parser);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "表达式嵌套深度超过限制") != NULL,
            "表达式深度诊断"
        );
        parser_leave_expression(&parser);
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-008 AST 节点预算
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-008", "AST 节点预算");
    {
        MKLexer lexer;
        MKParser parser;
        MKLimits limits;
        mk_limits_init(&limits);
        limits.max_ast_nodes = 1;

        CC_CHECK(
            parser_begin(&parser, &lexer, "", &limits),
            "初始化 AST 节点预算"
        );
        MKAST *ast = mk_ast_create("test.mk");
        CC_CHECK(ast != NULL, "创建预算测试 AST");
        if (ast)
        {
            parser.ast = ast;
            CC_CHECK(
                parser_new_node(&parser, MK_AST_TYPE, NULL) ==
                MK_AST_NODE_NIL,
                "超过 AST 节点预算失败"
            );
            const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
            CC_CHECK(
                diag && diag->message &&
                strstr(diag->message, "AST 节点数量超过限制") != NULL,
                "AST 节点预算诊断"
            );
            parser.ast = NULL;
            mk_ast_destroy(ast);
        }
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-009 声明预算
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-009", "声明预算");
    {
        MKLexer lexer;
        MKParser parser;
        MKLimits limits;
        mk_limits_init(&limits);
        limits.max_declarations = 1;

        CC_CHECK(
            parser_begin(&parser, &lexer, "", &limits),
            "初始化声明预算"
        );
        CC_CHECK(parser_count_declaration(&parser), "第一条声明");
        CC_CHECK(!parser_count_declaration(&parser), "超过声明预算失败");
        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "模块顶层声明数量超过限制") != NULL,
            "声明预算诊断"
        );
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-010 Token 缓冲、peek 与 expect
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-010", "Token 缓冲与 peek");
    {
        MKLexer lexer;
        MKParser parser;
        CC_CHECK(
            parser_begin(&parser, &lexer, "a b", NULL),
            "初始化 peek 输入"
        );

        MKToken next;
        memset(&next, 0, sizeof(next));
        CC_CHECK(parser_peek(&parser, &next), "读取前瞻 Token");
        CC_CHECK_EQ_I64(
            "前瞻类型", (CCINT64)MK_TOK_IDENT, (CCINT64)next.type
        );
        CC_CHECK(
            parser.current.len == 1 &&
            parser.current.text &&
            parser.current.text[0] == 'a',
            "当前 Token 未被消耗"
        );
        CC_CHECK_EQ_I64(
            "当前仍是第一个标识符",
            (CCINT64)MK_TOK_IDENT,
            (CCINT64)parser.current.type
        );
        CC_CHECK(parser_advance(&parser), "推进到前瞻 Token");
        CC_CHECK_EQ_I64(
            "推进后的 Token", (CCINT64)MK_TOK_IDENT,
            (CCINT64)parser.current.type
        );
        CC_CHECK(parser_advance(&parser), "推进到 EOF");
        CC_CHECK(
            parser_expect(&parser, MK_TOK_EOF, "文件结束"),
            "期望 EOF"
        );

        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);

        CC_CHECK(
            parser_begin(&parser, &lexer, "\"a\"", NULL),
            "初始化字符串 Token 输入"
        );
        CC_CHECK(
            !parser_peek(&parser, NULL),
            "字符串 Token 后禁止前瞻"
        );
        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "字符串 Token 后不允许前瞻") != NULL,
            "字符串前瞻诊断"
        );
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PARSER-011 expect 错误
     * ---------------------------------------------------------------- */
    CC_CASE("PARSER-011", "expect 错误");
    {
        MKLexer lexer;
        MKParser parser;
        CC_CHECK(
            parser_begin(&parser, &lexer, ";", NULL),
            "初始化 expect 输入"
        );
        CC_CHECK(
            !parser_expect(&parser, MK_TOK_IDENT, "标识符"),
            "期望失败"
        );
        const MKDiagnostic *diag = mk_parser_diagnostic(&parser);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "期望标识符") != NULL,
            "expect 诊断前缀"
        );
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "实际") != NULL,
            "expect 诊断包含实际 Token"
        );
        mk_parser_clear(&parser);
        mk_lexer_clear(&lexer);
    }
    CC_CASE_END();
}
