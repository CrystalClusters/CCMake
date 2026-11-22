/*
 * @Description: CMK 运行时测试：表达式、函数、循环、内建、target 与 Configure 错误。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "cmk_test_context.h"

#include <string.h>

static CCBOOL get_global(
    CMKTestContext *ctx, const char *name, CMKValue *out
)
{
    cmk_value_init(out);
    return cmk_runtime_get_global(
        ctx->runtime, name, (CCUINT32)strlen(name), out
    );
}

static const CMKTargetDraft *find_target(
    const CMKTestContext *ctx, const char *name
)
{
    CCUINT32 count = cmk_runtime_target_count(ctx->runtime);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKTargetDraft *draft =
            cmk_runtime_target_at(ctx->runtime, i);
        if (draft && draft->info &&
            draft->info->name.len == strlen(name) &&
            memcmp(draft->info->name.data, name, draft->info->name.len) == 0)
            return draft;
    }
    return NULL;
}

static CCBOOL set_int_array(
    CCVarTree *tree, const char *path,
    const CCINT64 *values, CCUINT32 count
)
{
    if (ccvar_set(
            tree, path, CC_VAR_ARRAY, NULL, 0, CC_VAR_WRITE_REPLACE
        ) != CC_VAR_OK)
        return CCFALSE;
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (ccvar_array_push(
                tree, path, CC_VAR_INT, &values[i], 0, NULL
            ) != CC_VAR_OK)
            return CCFALSE;
    }
    return CCTRUE;
}

void test_cmk_runtime(void)
{
    /* ------------------------------------------------------------------
     * RT-001 全局变量、函数与 while
     * ---------------------------------------------------------------- */
    CC_CASE("RT-001", "全局变量、函数与 while");
    {
        const char *source =
            "int total = 0;\n"
            "func add(int a, int b) -> int { return a + b; }\n"
            "func sum_to(int n) -> int {\n"
            "    int i = 0;\n"
            "    int s = 0;\n"
            "    while (i < n) { s += i; i += 1; }\n"
            "    return s;\n"
            "}\n"
            "int result = sum_to(5);\n"
            "int direct = add(2, 3);\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "语义通过");
        CC_CHECK(cmk_test_context_configure(&ctx), "Configure 通过");

        CMKValue value;
        CC_CHECK(get_global(&ctx, "result", &value), "读取 result");
        CC_CHECK(value.type == CMK_VALUE_INT && value.as.integer == 10,
                 "while 求和结果");
        cmk_value_clear(&value);

        CC_CHECK(get_global(&ctx, "direct", &value), "读取 direct");
        CC_CHECK(value.type == CMK_VALUE_INT && value.as.integer == 5,
                 "函数调用结果");
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * RT-002 列表、字符串与配置
     * ---------------------------------------------------------------- */
    CC_CASE("RT-002", "列表、字符串与配置");
    {
        const char *source =
            "int n = len(@{sources});\n"
            "string joined = join(@{sources}, \",\");\n"
            "string first = @{sources}[0];\n"
            "string base = basename(\"/a/b.c\");\n"
            "int calc = 2 + 3 * 4;\n"
            "bool ok = 2 < 3 && \"a\" == \"a\";\n"
            "list<string> all = @{sources} + {\"c.c\"};\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        const char *sources[] = {"a.c", "b.c"};
        CC_CHECK(
            cmk_test_set_string_array(ctx.tree, "sources", sources, 2),
            "配置 sources"
        );
        CC_CHECK(cmk_test_context_load(&ctx, source), "语义通过");
        CC_CHECK(cmk_test_context_configure(&ctx), "Configure 通过");

        CMKValue value;
        CC_CHECK(get_global(&ctx, "n", &value), "读取 n");
        CC_CHECK(value.type == CMK_VALUE_INT && value.as.integer == 2,
                 "len 结果");
        cmk_value_clear(&value);

        CC_CHECK(get_global(&ctx, "joined", &value), "读取 joined");
        CC_CHECK_EQ_STR("join 结果", "a.c,b.c", value.as.string.data);
        cmk_value_clear(&value);

        CC_CHECK(get_global(&ctx, "first", &value), "读取 first");
        CC_CHECK_EQ_STR("索引结果", "a.c", value.as.string.data);
        cmk_value_clear(&value);

        CC_CHECK(get_global(&ctx, "base", &value), "读取 base");
        CC_CHECK_EQ_STR("basename 结果", "b.c", value.as.string.data);
        cmk_value_clear(&value);

        CC_CHECK(get_global(&ctx, "calc", &value), "读取 calc");
        CC_CHECK(value.as.integer == 14, "算术优先级");
        cmk_value_clear(&value);

        CC_CHECK(get_global(&ctx, "ok", &value), "读取 ok");
        CC_CHECK(value.type == CMK_VALUE_BOOL && value.as.boolean,
                 "逻辑与字符串比较");
        cmk_value_clear(&value);

        CC_CHECK(get_global(&ctx, "all", &value), "读取 all");
        CC_CHECK_EQ_I64("列表拼接长度", 3,
                        (CCINT64)cmk_list_count(value.as.list));
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * RT-003 递归
     * ---------------------------------------------------------------- */
    CC_CASE("RT-003", "递归");
    {
        const char *source =
            "func fact(int n) -> int {\n"
            "    if (n <= 1) { return 1; }\n"
            "    return n * fact(n - 1);\n"
            "}\n"
            "int result = fact(5);\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "语义通过");
        CC_CHECK(cmk_test_context_configure(&ctx), "Configure 通过");

        CMKValue value;
        CC_CHECK(get_global(&ctx, "result", &value), "读取阶乘");
        CC_CHECK(value.as.integer == 120, "递归结果");
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * RT-004 target 与动作
     * ---------------------------------------------------------------- */
    CC_CASE("RT-004", "target 与动作");
    {
        const char *source =
            "target app() {\n"
            "    list<target_ref> depends_on = {core};\n"
            "    list<string> inputs = {\"a.c\"};\n"
            "    list<string> outputs = {\"app\"};\n"
            "    list<action> actions = {\n"
            "        mkdir(\"build\"),\n"
            "        command(\"cc\", {\"-O2\", \"a.c\"})\n"
            "    };\n"
            "}\n"
            "target core() {}\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "语义通过");
        CC_CHECK(cmk_test_context_configure(&ctx), "Configure 通过");
        CC_CHECK_EQ_I64("target 数", 2,
                        (CCINT64)cmk_runtime_target_count(ctx.runtime));

        const CMKTargetDraft *app = find_target(&ctx, "app");
        CC_CHECK(app != NULL, "找到 app draft");
        if (app)
        {
            CC_CHECK_EQ_I64(
                "depends_on 数", 1,
                (CCINT64)cmk_list_count(app->depends_on.as.list)
            );
            const CMKValue *dep =
                cmk_list_at(app->depends_on.as.list, 0);
            CC_CHECK(
                dep && dep->type == CMK_VALUE_TARGET_REF &&
                strcmp(dep->as.target_ref.name.data, "core") == 0,
                "target_ref 值"
            );
            CC_CHECK_EQ_I64(
                "action 数", 2,
                (CCINT64)cmk_list_count(app->actions.as.list)
            );
            const CMKValue *mkdir =
                cmk_list_at(app->actions.as.list, 0);
            const CMKValue *command =
                cmk_list_at(app->actions.as.list, 1);
            CC_CHECK(
                mkdir && mkdir->type == CMK_VALUE_ACTION &&
                mkdir->as.action->kind == CMK_ACTION_MKDIR,
                "mkdir 动作"
            );
            CC_CHECK(
                command && command->type == CMK_VALUE_ACTION &&
                command->as.action->kind == CMK_ACTION_EXEC &&
                strcmp(command->as.action->program.data, "cc") == 0 &&
                cmk_list_count(command->as.action->args) == 2,
                "command 动作"
            );
        }
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * RT-005 顶层 if 选择 target
     * ---------------------------------------------------------------- */
    CC_CASE("RT-005", "顶层 if 选择 target");
    {
        const char *source =
            "if (@{platform} == \"windows\") {\n"
            "    target app() { list<string> outputs = {\"a.exe\"}; }\n"
            "} else {\n"
            "    target app() { list<string> outputs = {\"a\"}; }\n"
            "}\n";

        {
            CMKTestContext ctx;
            cmk_test_context_init(&ctx);
            CC_CHECK(
                cmk_test_set_string(ctx.tree, "platform", "windows"),
                "配置 windows"
            );
            CC_CHECK(cmk_test_context_load(&ctx, source), "语义通过");
            CC_CHECK(cmk_test_context_configure(&ctx), "Configure 通过");
            CC_CHECK_EQ_I64(
                "windows target 数", 1,
                (CCINT64)cmk_runtime_target_count(ctx.runtime)
            );
            const CMKTargetDraft *app = find_target(&ctx, "app");
            CC_CHECK(app != NULL, "windows app");
            if (app)
            {
                const CMKValue *output =
                    cmk_list_at(app->outputs.as.list, 0);
                CC_CHECK_EQ_STR("windows 输出", "a.exe", output->as.string.data);
            }
            cmk_test_context_clear(&ctx);
        }

        {
            CMKTestContext ctx;
            cmk_test_context_init(&ctx);
            CC_CHECK(
                cmk_test_set_string(ctx.tree, "platform", "linux"),
                "配置 linux"
            );
            CC_CHECK(cmk_test_context_load(&ctx, source), "语义通过");
            CC_CHECK(cmk_test_context_configure(&ctx), "Configure 通过");
            const CMKTargetDraft *app = find_target(&ctx, "app");
            CC_CHECK(app != NULL, "linux app");
            if (app)
            {
                const CMKValue *output =
                    cmk_list_at(app->outputs.as.list, 0);
                CC_CHECK_EQ_STR("linux 输出", "a", output->as.string.data);
            }
            cmk_test_context_clear(&ctx);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * RT-006 for-in / break / continue
     * ---------------------------------------------------------------- */
    CC_CASE("RT-006", "for-in / break / continue");
    {
        const char *source =
            "func process() -> int {\n"
            "    int sum = 0;\n"
            "    for (int x in @{nums}) {\n"
            "        if (x == 2) { continue; }\n"
            "        if (x == 4) { break; }\n"
            "        sum += x;\n"
            "    }\n"
            "    return sum;\n"
            "}\n"
            "int result = process();\n";

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CCINT64 nums[] = {1, 2, 3, 4, 5};
        CC_CHECK(set_int_array(ctx.tree, "nums", nums, 5), "配置 nums");
        CC_CHECK(cmk_test_context_load(&ctx, source), "语义通过");
        CC_CHECK(cmk_test_context_configure(&ctx), "Configure 通过");

        CMKValue value;
        CC_CHECK(get_global(&ctx, "result", &value), "读取 result");
        CC_CHECK(value.as.integer == 4, "for-in 结果");
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * RT-007 Configure 错误
     * ---------------------------------------------------------------- */
    CC_CASE("RT-007", "Configure 错误");
    {
        const char *index_source =
            "func f() -> string { list<string> x = {\"a\"}; return x[2]; }\n"
            "string r = f();\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, index_source), "索引语义通过");
        CC_CHECK(!cmk_test_context_configure(&ctx), "索引越界失败");
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "越界") != NULL,
            "索引越界诊断"
        );
        cmk_test_context_clear(&ctx);

        cmk_test_context_init(&ctx);
        CC_CHECK(
            cmk_test_context_load(&ctx, "int x = 1 / 0;"),
            "除零语义通过"
        );
        CC_CHECK(!cmk_test_context_configure(&ctx), "除零失败");
        diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "除数为零") != NULL,
            "除零诊断"
        );
        cmk_test_context_clear(&ctx);

        cmk_test_context_init(&ctx);
        CC_CHECK(
            cmk_test_context_load(
                &ctx,
                "func f() -> int { error(\"boom %s\", \"x\"); }\n"
                "int r = f();\n"
            ),
            "error 语义通过"
        );
        CC_CHECK(!cmk_test_context_configure(&ctx), "error 终止配置");
        diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "boom x") != NULL,
            "error 格式文本"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();
}
