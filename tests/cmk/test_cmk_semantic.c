/*
 * @Description: CMK 语义分析测试：作用域、类型、函数、内建、target 与配置。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "cmk_test_context.h"
#include "cmk/cmk_semantic_internal.h"

#include <ccalloc.h>

#include <string.h>

static void check_error(
    const char *source, const char *expected
)
{
    CMKTestContext ctx;
    cmk_test_context_init(&ctx);
    CC_CHECK(!cmk_test_context_load(&ctx, source), "语义分析应失败");
    const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
    CC_CHECK(
        diag && diag->message &&
        strstr(diag->message, expected) != NULL,
        "语义诊断匹配"
    );
    cmk_test_context_clear(&ctx);
}

void test_cmk_semantic(void)
{
    /* ------------------------------------------------------------------
     * SEM-001 模块作用域与函数
     * ---------------------------------------------------------------- */
    CC_CASE("SEM-001", "模块作用域与函数");
    {
        const char *source =
            "const int limit = 3;\n"
            "int base = 2;\n"
            "func add(int a, int b) -> int { return a + b; }\n"
            "func use(int n) -> int {\n"
            "    int total = 0;\n"
            "    int i = 0;\n"
            "    while (i < n) { total += add(i, base); i += 1; }\n"
            "    return total;\n"
            "}\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "解析并分析模块");

        const CMKFunctionInfo *add = cmk_semantic_find_function(
            ctx.semantic, "add", 3
        );
        CC_CHECK(add != NULL, "找到 add");
        if (add)
        {
            CC_CHECK_EQ_I64("add 参数数", 2, (CCINT64)add->param_count);
            CC_CHECK(
                add->return_type.kind == CMK_VALUE_INT,
                "add 返回 int"
            );
        }
        const CMKFunctionInfo *use = cmk_semantic_find_function(
            ctx.semantic, "use", 3
        );
        CC_CHECK(use != NULL, "找到 use");
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * SEM-002 类型与作用域错误
     * ---------------------------------------------------------------- */
    CC_CASE("SEM-002", "类型与作用域错误");
    {
        check_error("int x = \"s\";", "类型不匹配");
        check_error(
            "list<string> xs = {1};", "list 元素类型不一致"
        );
        check_error("int x = 1; int x = 2;", "重复");
        check_error(
            "func f() -> void { int x = 1; x = \"s\"; }",
            "类型不匹配"
        );
        check_error(
            "func f() -> void { const int x = 1; x = 2; }",
            "const"
        );
        check_error("int y = missing;", "未定义");
        check_error(
            "func f() -> int { return; }", "必须返回值"
        );
        check_error(
            "func f() -> void { return 1; }", "void 函数不能返回"
        );
        check_error(
            "func f() -> int { }", "缺少返回值"
        );
        check_error(
            "func f() -> void { break; }", "循环"
        );
        check_error(
            "func f(int a, int a) -> void {}", "参数重名"
        );
        check_error(
            "func f() -> void { if (1) {} }", "if 条件"
        );
        check_error(
            "func f() -> void { while (1) {} }", "while 条件"
        );
        check_error(
            "func f() -> void { int x = 1; int x = 2; }", "重复"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * SEM-003 函数与内建
     * ---------------------------------------------------------------- */
    CC_CASE("SEM-003", "函数与内建");
    {
        const char *source =
            "func f(int n) -> int { return n + 1; }\n"
            "int value = f(1);\n"
            "int n = len(@{sources});\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        const char *sources[] = {"a.c", "b.c"};
        CC_CHECK(
            cmk_test_set_string_array(ctx.tree, "sources", sources, 2),
            "配置 sources"
        );
        CC_CHECK(cmk_test_context_load(&ctx, source), "函数与内建语义");
        cmk_test_context_clear(&ctx);

        check_error(
            "int n = len(\"x\");", "内建实参类型错误"
        );
        check_error(
            "int n = len();", "参数数量错误"
        );
        check_error(
            "func f(int a) -> void {} func g() -> void { f(); }",
            "需要 1 个参数"
        );
        check_error(
            "func f(int a) -> void {} func g() -> void { f(\"x\"); }",
            "类型不匹配"
        );
        check_error(
            "func g() -> void { missing(); }", "未定义"
        );
        check_error(
            "func len(int a) -> int { return a; }", "内建"
        );
        check_error(
            "int x = 1 + \"s\";", "加法"
        );
    }
    CC_CASE_END();



    CC_CASE("SEM-004", "深语句");
    {
        const CCUINT32 depth = 5000;
        size_t cap = (size_t)depth * 2 + 64;
        char *src = ccalloc(NULL, cap, NULL);
        CC_CHECK(src != NULL, "分配源码");
        if (src)
        {
            size_t used = (size_t)snprintf(src, cap, "func main() -> void {");
            for (CCUINT32 i = 0; i < depth; i++)
                src[used++] = '{';
            for (CCUINT32 i = 0; i < depth; i++)
                src[used++] = '}';
            src[used++] = '}';
            src[used] = '\0';
            CMKTestContext ctx;
            cmk_test_context_init(&ctx);
            CC_CHECK(cmk_test_context_load(&ctx, src), "语义分析深嵌套块");
            cmk_test_context_clear(&ctx);
            ccalloc(src, 0, NULL);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * SEM-005 配置引用
     * ---------------------------------------------------------------- */
    CC_CASE("SEM-005", "配置引用");
    {
        const char *source =
            "string name = @{name};\n"
            "int count = @{count};\n"
            "list<string> items = @{items};\n"
            "list<string> empty = @{empty};\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_set_string(ctx.tree, "name", "app"), "配置 name");
        CC_CHECK(cmk_test_set_int(ctx.tree, "count", 3), "配置 count");
        const char *items[] = {"a", "b"};
        CC_CHECK(
            cmk_test_set_string_array(ctx.tree, "items", items, 2),
            "配置 items"
        );
        CC_CHECK(
            cmk_test_set_string_array(ctx.tree, "empty", NULL, 0),
            "配置空数组"
        );
        CC_CHECK(cmk_test_context_load(&ctx, source), "配置引用语义");
        cmk_test_context_clear(&ctx);

        cmk_test_context_init(&ctx);
        CC_CHECK(
            !cmk_test_context_load(&ctx, "int x = @{missing};"),
            "缺失配置失败"
        );
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "不存在") != NULL,
            "缺失配置诊断"
        );
        cmk_test_context_clear(&ctx);

        cmk_test_context_init(&ctx);
        CC_CHECK(
            ccvar_set(
                ctx.tree, "mixed", CC_VAR_ARRAY, NULL, 0,
                CC_VAR_WRITE_REPLACE
            ) == CC_VAR_OK,
            "配置 mixed 数组"
        );
        CC_CHECK(
            ccvar_array_push(
                ctx.tree, "mixed", CC_VAR_STRING, "x", 1, NULL
            ) == CC_VAR_OK,
            "配置 mixed 首项"
        );
        CC_CHECK(
            ccvar_array_push(
                ctx.tree, "mixed", CC_VAR_INT, &(CCINT64){1}, 0, NULL
            ) == CC_VAR_OK,
            "配置 mixed 第二项"
        );
        CC_CHECK(
            !cmk_test_context_load(&ctx, "list<string> x = @{mixed};"),
            "混合数组失败"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();


}
