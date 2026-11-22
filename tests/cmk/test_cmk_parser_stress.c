/*
 * @Description: CMK Parser 压力与集成测试：深嵌套、预算、确定性随机输入与失败后复用。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "cmk_parser_test_util.h"

#include <stdio.h>
#include <string.h>

static void check_limited_error(
    const char *src, const CMKLimits *limits, const char *expected
)
{
    CMKTestParse ctx;
    CC_CHECK(
        !cmk_test_parse_begin(&ctx, src, limits),
        "受限解析应失败"
    );
    CC_CHECK(ctx.ast == NULL, "受限解析不发布 AST");
    const CMKDiagnostic *diag = cmk_parser_diagnostic(&ctx.parser);
    CC_CHECK(
        diag && diag->message &&
        strstr(diag->message, expected) != NULL,
        "受限解析诊断匹配"
    );
    cmk_test_parse_end(&ctx);
}

void test_cmk_parser_stress(void)
{
    /* ------------------------------------------------------------------
     * STRESS-001 语法与表达式深度
     * ---------------------------------------------------------------- */
    CC_CASE("STRESS-001", "语法与表达式深度");
    {
        CMKLimits limits;
        cmk_limits_init(&limits);
        limits.max_syntax_depth = 2;
        check_limited_error(
            "func f() -> void { if (a) {} }",
            &limits, "语法嵌套深度超过限制"
        );

        cmk_limits_init(&limits);
        limits.max_expression_depth = 3;
        check_limited_error(
            "int x = ((((1))));",
            &limits, "表达式嵌套深度超过限制"
        );

        // 默认预算下同一输入可解析
        CMKTestParse ctx;
        CC_CHECK(
            cmk_test_parse_begin(&ctx, "int x = ((((1))));", NULL),
            "默认预算解析嵌套表达式"
        );
        cmk_test_parse_end(&ctx);

        // 类型递归也必须消耗语法深度预算
        cmk_limits_init(&limits);
        limits.max_syntax_depth = 4;
        CC_CHECK(
            cmk_test_parse_begin(
                &ctx, "list<list<int>> x = {};", &limits
            ),
            "类型深度恰好达到限制"
        );
        if (ctx.ast)
        {
            const char *reason = NULL;
            CC_CHECK(cmk_ast_validate(ctx.ast, &reason), "类型深度 AST 校验");
        }
        cmk_test_parse_end(&ctx);

        check_limited_error(
            "list<list<list<int>>> x = {};",
            &limits, "语法嵌套深度超过限制"
        );

        char deep_type[512];
        size_t pos = 0;
        for (CCUINT32 i = 0; i < 32; i++)
        {
            memcpy(deep_type + pos, "list<", 5);
            pos += 5;
        }
        memcpy(deep_type + pos, "int", 3);
        pos += 3;
        for (CCUINT32 i = 0; i < 32; i++)
            deep_type[pos++] = '>';
        memcpy(deep_type + pos, " x = {};", 8);
        pos += 8;
        deep_type[pos] = '\0';

        cmk_limits_init(&limits);
        limits.max_syntax_depth = 8;
        check_limited_error(
            deep_type, &limits, "语法嵌套深度超过限制"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STRESS-002 各类预算
     * ---------------------------------------------------------------- */
    CC_CASE("STRESS-002", "各类预算");
    {
        CMKLimits limits;
        cmk_limits_init(&limits);
        limits.max_function_params = 1;
        check_limited_error(
            "func f(int a, int b) -> void {}",
            &limits, "函数参数数量超过限制"
        );

        cmk_limits_init(&limits);
        limits.max_list_elements = 2;
        check_limited_error(
            "int x = {1, 2, 3};",
            &limits, "列表元素数量超过限制"
        );

        cmk_limits_init(&limits);
        limits.max_declarations = 1;
        check_limited_error(
            "int a = 1; int b = 2;",
            &limits, "模块顶层声明数量超过限制"
        );

        cmk_limits_init(&limits);
        limits.max_ast_nodes = 2;
        check_limited_error(
            "int a = 1;",
            &limits, "AST 节点数量超过限制"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STRESS-003 默认预算下的嵌套块
     * ---------------------------------------------------------------- */
    CC_CASE("STRESS-003", "默认预算下的嵌套块");
    {
        char source[4096];
        size_t pos = 0;
        const char *head = "func f() -> void {";
        memcpy(source + pos, head, strlen(head));
        pos += strlen(head);
        for (CCUINT32 i = 0; i < 100; i++)
            source[pos++] = '{';
        for (CCUINT32 i = 0; i < 100; i++)
            source[pos++] = '}';
        source[pos++] = '}';
        source[pos] = '\0';

        CMKTestParse ctx;
        CC_CHECK(cmk_test_parse_begin(&ctx, source, NULL), "解析深嵌套块");
        if (ctx.ast)
        {
            const char *reason = NULL;
            CC_CHECK(cmk_ast_validate(ctx.ast, &reason), "深嵌套 AST 校验");
        }
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STRESS-004 大量顶层声明
     * ---------------------------------------------------------------- */
    CC_CASE("STRESS-004", "大量顶层声明");
    {
        char source[8192];
        size_t pos = 0;
        for (CCUINT32 i = 0; i < 200; i++)
        {
            int n = snprintf(
                source + pos, sizeof(source) - pos,
                "int v%u = %u;\n", i, i
            );
            if (n < 0 || (size_t)n >= sizeof(source) - pos)
                break;
            pos += (size_t)n;
        }

        CMKTestParse ctx;
        CC_CHECK(cmk_test_parse_begin(&ctx, source, NULL), "解析大量声明");
        if (ctx.ast)
        {
            CC_CHECK_EQ_I64(
                "声明数量", 200,
                (CCINT64)cmk_test_child_count(
                    &ctx, cmk_ast_root(ctx.ast)
                )
            );
            const char *reason = NULL;
            CC_CHECK(cmk_ast_validate(ctx.ast, &reason), "大量声明 AST 校验");
        }
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STRESS-005 失败后复用
     * ---------------------------------------------------------------- */
    CC_CASE("STRESS-005", "失败后复用");
    {
        CMKTestParse ctx;
        CC_CHECK(
            !cmk_test_parse_begin(&ctx, "int x = ;", NULL),
            "失败源码"
        );
        CC_CHECK(ctx.ast == NULL, "失败不发布 AST");
        cmk_test_parse_end(&ctx);

        CC_CHECK(
            cmk_test_parse_begin(&ctx, "int x = 1;", NULL),
            "复用上下文解析成功"
        );
        if (ctx.ast)
        {
            const char *reason = NULL;
            CC_CHECK(cmk_ast_validate(ctx.ast, &reason), "复用后 AST 校验");
        }
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STRESS-006 随机字节输入
     * ---------------------------------------------------------------- */
    CC_CASE("STRESS-006", "随机字节输入");
    {
        CMKLimits limits;
        cmk_limits_init(&limits);
        limits.max_tokens = 4096;

        CCUINT32 state = 0x9E3779B9u;
        CCUINT32 parse_success = 0;
        CCUINT32 bad_cases = 0;

        for (CCUINT32 iter = 0; iter < 1000; iter++)
        {
            state = state * 1664525u + 1013904223u;
            CCUINT32 len = state % 257u;
            char buffer[257];
            for (CCUINT32 i = 0; i < len; i++)
            {
                state = state * 1664525u + 1013904223u;
                buffer[i] = (char)(state >> 24);
            }

            CMKLexer lexer;
            CMKParser parser;
            CMKAST *ast = NULL;
            if (!cmk_lexer_init(&lexer, "random.cmk", buffer, len))
            {
                cmk_lexer_clear(&lexer);
                bad_cases++;
                continue;
            }
            if (!cmk_parser_init(&parser, &lexer, &limits))
            {
                cmk_parser_clear(&parser);
                cmk_lexer_clear(&lexer);
                continue;
            }

            if (cmk_parser_parse(&parser, &ast))
            {
                parse_success++;
                if (!ast)
                {
                    bad_cases++;
                }
                else
                {
                    const char *reason = NULL;
                    if (!cmk_ast_validate(ast, &reason))
                        bad_cases++;
                    cmk_ast_destroy(ast);
                }
            }
            else if (ast != NULL)
            {
                bad_cases++;
            }

            cmk_parser_clear(&parser);
            cmk_lexer_clear(&lexer);
        }

        CC_CHECK_EQ_I64("随机输入异常用例", 0, bad_cases);
        CC_CHECK(parse_success <= 1000, "随机输入计数有界");
    }
    CC_CASE_END();
}
