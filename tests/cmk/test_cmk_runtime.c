/*
 * @Description: CMK 运行时测试：表达式、函数、循环、内建、target 与 Configure 错误。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "cmk_test_context.h"
#include "cmk/cmk_exec_stack.h"

#include <ccalloc.h>

#include <string.h>

static void test_runtime_deep_statements(void);

static CCBOOL exec_stack_probe(void *user)
{
    CCBOOL *flags = (CCBOOL *)user;
    flags[0] = CCTRUE;
    flags[1] = cmk_exec_stack_check((size_t)1024);
    return CCTRUE;
}

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

    /* ------------------------------------------------------------------
     * RT-008 可配置执行策略
     * ---------------------------------------------------------------- */
    CC_CASE("RT-008", "可配置执行策略");
    {
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(
            cmk_test_context_load(
                &ctx,
                "int a = 1;\nint b = 2;\nint c = 3;\n"
            ),
            "策略用例语义通过"
        );
        ctx.runtime = cmk_runtime_create(
            ctx.semantic, &ctx.config, ctx.builtins, &ctx.diagnostic
        );
        CC_CHECK(ctx.runtime != NULL, "策略运行时创建");
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_steps = 2;
        cmk_runtime_set_policy(ctx.runtime, &policy);
        CC_CHECK(!cmk_runtime_configure(ctx.runtime), "步数预算触发失败");
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "执行步数") != NULL,
            "步数预算诊断"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();
    test_runtime_deep_statements();
}

static void test_runtime_deep_statements(void)
{
    CC_CASE("RT-009", "显式语句栈深嵌套");
    char *source = ccalloc(NULL, 200000, "CMK 运行时深语句测试");
    CC_CHECK(source != NULL, "运行时深语句缓冲区");
    if (source)
    {
        size_t pos = 0;
        const char *head = "func f() -> int {";
        memcpy(source + pos, head, strlen(head));
        pos += strlen(head);
        for (CCUINT32 i = 0; i < 5000; i++)
            source[pos++] = '{';
        for (CCUINT32 i = 0; i < 5000; i++)
            source[pos++] = '}';
        const char *tail =
            "return 1; }\nint g = f();\ntarget core() {}\n";
        memcpy(source + pos, tail, strlen(tail));
        pos += strlen(tail);
        source[pos] = '\0';

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(
            cmk_test_context_load(&ctx, source),
            "5000 层块语义"
        );
        CC_CHECK(
            cmk_test_context_configure(&ctx),
            "5000 层块 Configure"
        );
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(get_global(&ctx, "g", &value), "深块全局变量");
        CC_CHECK(
            value.type == CMK_VALUE_INT &&
            value.as.integer == 1,
            "深块返回值"
        );
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
        ccalloc(source, 0, NULL);
    }

    source = ccalloc(NULL, 200000, "CMK 运行时深 if 测试");
    CC_CHECK(source != NULL, "运行时深 if 缓冲区");
    if (source)
    {
        size_t pos = 0;
        const char *head = "func f() -> int {";
        memcpy(source + pos, head, strlen(head));
        pos += strlen(head);
        for (CCUINT32 i = 0; i < 5000; i++)
        {
            const char *if_head = "if (true) {";
            memcpy(source + pos, if_head, strlen(if_head));
            pos += strlen(if_head);
        }
        const char *body = "return 1;";
        memcpy(source + pos, body, strlen(body));
        pos += strlen(body);
        for (CCUINT32 i = 0; i < 5000; i++)
            source[pos++] = '}';
        const char *tail =
            "return 1; }\nint g = f();\ntarget core() {}\n";
        memcpy(source + pos, tail, strlen(tail));
        pos += strlen(tail);
        source[pos] = '\0';

        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(
            cmk_test_context_load(&ctx, source),
            "5000 层 if 语义"
        );
        CC_CHECK(
            cmk_test_context_configure(&ctx),
            "5000 层 if Configure"
        );
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(get_global(&ctx, "g", &value), "深 if 全局变量");
        CC_CHECK(
            value.type == CMK_VALUE_INT &&
            value.as.integer == 1,
            "深 if 返回值"
        );
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
        ccalloc(source, 0, NULL);
    }
    CC_CASE_END();

    CC_CASE("RT-010", "递归深度策略");
    {
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(
            cmk_test_context_load(
                &ctx,
                "func f(int n) -> int {\n"
                "    if (n <= 0) { return 0; }\n"
                "    return f(n - 1);\n"
                "}\n"
                "int g = f(100);\n"
                "target core() {}\n"
            ),
            "递归策略语义通过"
        );
        ctx.runtime = cmk_runtime_create(
            ctx.semantic, &ctx.config, ctx.builtins, &ctx.diagnostic
        );
        CC_CHECK(ctx.runtime != NULL, "递归策略运行时创建");
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_recursion_depth = 8;
        cmk_runtime_set_policy(ctx.runtime, &policy);
        CC_CHECK(
            !cmk_runtime_configure(ctx.runtime),
            "递归深度预算触发失败"
        );
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "递归深度") != NULL,
            "递归深度预算诊断"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-011", "显式顶层 target 栈");
    {
        char *source = ccalloc(NULL, 200000, "CMK 运行时顶层 if 测试");
        CC_CHECK(source != NULL, "运行时顶层 if 缓冲区");
        if (source)
        {
            size_t pos = 0;
            for (CCUINT32 i = 0; i < 5000; i++)
            {
                const char *head = "if (true) {";
                memcpy(source + pos, head, strlen(head));
                pos += strlen(head);
            }
            const char *target = "target core() {}";
            memcpy(source + pos, target, strlen(target));
            pos += strlen(target);
            for (CCUINT32 i = 0; i < 5000; i++)
                source[pos++] = '}';
            source[pos] = '\0';

            CMKTestContext ctx;
            cmk_test_context_init(&ctx);
            CC_CHECK(
                cmk_test_context_load(&ctx, source),
                "5000 层顶层 if 语义"
            );
            CC_CHECK(
                cmk_test_context_configure(&ctx),
                "5000 层顶层 if Configure"
            );
            cmk_test_context_clear(&ctx);
            ccalloc(source, 0, NULL);
        }
    }
    CC_CASE_END();

    CC_CASE("RT-012", "显式表达式栈深嵌套");
    {
        char *source = ccalloc(NULL, 200000, "CMK 运行时深表达式测试");
        CC_CHECK(source != NULL, "运行时深表达式缓冲区");
        if (source)
        {
            size_t pos = 0;
            const char *head = "int a = 1";
            memcpy(source + pos, head, strlen(head));
            pos += strlen(head);
            for (CCUINT32 i = 0; i < 5000; i++)
            {
                memcpy(source + pos, " + 1", 4);
                pos += 4;
            }
            const char *body =
                ";\nbool b = ";
            memcpy(source + pos, body, strlen(body));
            pos += strlen(body);
            for (CCUINT32 i = 0; i < 5000; i++)
                source[pos++] = '!';
            const char *list_head = "true;\nlist<int> l = {";
            memcpy(source + pos, list_head, strlen(list_head));
            pos += strlen(list_head);
            for (CCUINT32 i = 0; i < 5000; i++)
            {
                source[pos++] = '1';
                if (i + 1 < 5000)
                    source[pos++] = ',';
            }
            const char *tail =
                "};\nint c = l[0];\ntarget core() {}\n";
            memcpy(source + pos, tail, strlen(tail));
            pos += strlen(tail);
            source[pos] = '\0';

            CMKTestContext ctx;
            cmk_test_context_init(&ctx);
            CC_CHECK(
                cmk_test_context_load(&ctx, source),
                "5000 层表达式语义"
            );
            CC_CHECK(
                cmk_test_context_configure(&ctx),
                "5000 层表达式 Configure"
            );
            CMKValue value;
            cmk_value_init(&value);
            CC_CHECK(get_global(&ctx, "a", &value), "读取 a");
            CC_CHECK(
                value.type == CMK_VALUE_INT &&
                value.as.integer == 5001,
                "深二元结果"
            );
            cmk_value_clear(&value);
            cmk_value_init(&value);
            CC_CHECK(get_global(&ctx, "b", &value), "读取 b");
            CC_CHECK(
                value.type == CMK_VALUE_BOOL &&
                value.as.boolean == CCTRUE,
                "深一元结果"
            );
            cmk_value_clear(&value);
            cmk_value_init(&value);
            CC_CHECK(get_global(&ctx, "c", &value), "读取 c");
            CC_CHECK(
                value.type == CMK_VALUE_INT &&
                value.as.integer == 1,
                "深列表索引结果"
            );
            cmk_value_clear(&value);
            cmk_test_context_clear(&ctx);
            ccalloc(source, 0, NULL);
        }
    }
    CC_CASE_END();

    CC_CASE("RT-013", "显式调用栈深嵌套");
    {
        char *source = ccalloc(NULL, 200000, "CMK 运行时深调用测试");
        CC_CHECK(source != NULL, "运行时深调用缓冲区");
        if (source)
        {
            size_t pos = 0;
            const char *head =
                "func f(int n) -> int { return n; }\nint g = ";
            memcpy(source + pos, head, strlen(head));
            pos += strlen(head);
            for (CCUINT32 i = 0; i < 5000; i++)
            {
                memcpy(source + pos, "f(", 2);
                pos += 2;
            }
            source[pos++] = '1';
            for (CCUINT32 i = 0; i < 5000; i++)
                source[pos++] = ')';
            const char *tail = ";\ntarget core() {}\n";
            memcpy(source + pos, tail, strlen(tail));
            pos += strlen(tail);
            source[pos] = '\0';

            CMKTestContext ctx;
            cmk_test_context_init(&ctx);
            CC_CHECK(
                cmk_test_context_load(&ctx, source),
                "5000 层调用语义"
            );
            CC_CHECK(
                cmk_test_context_configure(&ctx),
                "5000 层调用 Configure"
            );
            CMKValue value;
            cmk_value_init(&value);
            CC_CHECK(get_global(&ctx, "g", &value), "读取 g");
            CC_CHECK(
                value.type == CMK_VALUE_INT &&
                value.as.integer == 1,
                "深调用结果"
            );
            cmk_value_clear(&value);
            cmk_test_context_clear(&ctx);
            ccalloc(source, 0, NULL);
        }
    }
    CC_CASE_END();

    CC_CASE("RT-014", "尾递归不消耗 C 栈");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    if (n <= 0) { return 0; }\n"
            "    return f(n - 1);\n"
            "}\n"
            "int g = f(5000);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "尾递归语义");
        CC_CHECK(cmk_test_context_configure(&ctx), "100000 层尾递归 Configure");
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(get_global(&ctx, "g", &value), "读取尾递归结果");
        CC_CHECK(
            value.type == CMK_VALUE_INT &&
            value.as.integer == 0,
            "尾递归返回值"
        );
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-015", "非尾递归深度策略");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    if (n <= 0) { return 1; }\n"
            "    return n * f(n - 1);\n"
            "}\n"
            "int g = f(1000);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "非尾递归语义");
        ctx.runtime = cmk_runtime_create(
            ctx.semantic, &ctx.config, ctx.builtins, &ctx.diagnostic
        );
        CC_CHECK(ctx.runtime != NULL, "非尾递归运行时创建");
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_recursion_depth = 20;
        cmk_runtime_set_policy(ctx.runtime, &policy);
        CC_CHECK(
            !cmk_runtime_configure(ctx.runtime),
            "非尾递归深度预算触发"
        );
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "递归深度") != NULL,
            "非尾递归深度诊断"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-016", "非尾线性递归延续栈");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    if (n <= 0) { return 0; }\n"
            "    return 1 + f(n - 1);\n"
            "}\n"
            "int g = f(1000);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "线性递归语义");
        CC_CHECK(cmk_test_context_configure(&ctx), "线性递归 Configure");
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(get_global(&ctx, "g", &value), "读取线性递归结果");
        CC_CHECK(
            value.type == CMK_VALUE_INT &&
            value.as.integer == 1000,
            "线性递归返回值"
        );
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-017", "分支递归深度策略");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    if (n <= 0) { return 0; }\n"
            "    if (n == 1) { return 1; }\n"
            "    return f(n - 1) + f(n - 2);\n"
            "}\n"
            "int g = f(1000);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "分支递归语义");
        ctx.runtime = cmk_runtime_create(
            ctx.semantic, &ctx.config, ctx.builtins, &ctx.diagnostic
        );
        CC_CHECK(ctx.runtime != NULL, "分支递归运行时创建");
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_recursion_depth = 20;
        cmk_runtime_set_policy(ctx.runtime, &policy);
        CC_CHECK(
            !cmk_runtime_configure(ctx.runtime),
            "分支递归深度预算触发"
        );
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "递归深度") != NULL,
            "分支递归深度诊断"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-018", "分支递归程序栈");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    if (n <= 0) { return 0; }\n"
            "    if (n == 1) { return 1; }\n"
            "    return f(n - 1) + f(n - 2);\n"
            "}\n"
            "int g = f(15);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "分支递归程序语义");
        CC_CHECK(
            cmk_test_context_configure(&ctx),
            "分支递归程序 Configure"
        );
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(get_global(&ctx, "g", &value), "读取分支递归结果");
        CC_CHECK(
            value.type == CMK_VALUE_INT &&
            value.as.integer == 610,
            "分支递归返回值"
        );
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-019", "非 return 表达式递归深度策略");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    int x = 0;\n"
            "    if (n > 0) { x = f(n - 1); }\n"
            "    return x;\n"
            "}\n"
            "int g = f(1000);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "赋值递归语义");
        ctx.runtime = cmk_runtime_create(
            ctx.semantic, &ctx.config, ctx.builtins, &ctx.diagnostic
        );
        CC_CHECK(ctx.runtime != NULL, "赋值递归运行时创建");
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_recursion_depth = 20;
        cmk_runtime_set_policy(ctx.runtime, &policy);
        CC_CHECK(
            !cmk_runtime_configure(ctx.runtime),
            "赋值递归深度预算触发"
        );
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "递归深度") != NULL,
            "赋值递归深度诊断"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-020", "Configure 时间策略");
    {
        const char *source =
            "func tick() -> int {\n"
            "    int i = 0;\n"
            "    while (i < 2000000) { i += 1; }\n"
            "    return i;\n"
            "}\n"
            "int g = tick();\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "时间策略语义");
        ctx.runtime = cmk_runtime_create(
            ctx.semantic, &ctx.config, ctx.builtins, &ctx.diagnostic
        );
        CC_CHECK(ctx.runtime != NULL, "时间策略运行时创建");
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_configure_time_ms = 1;
        cmk_runtime_set_policy(ctx.runtime, &policy);
        CC_CHECK(
            !cmk_runtime_configure(ctx.runtime),
            "Configure 时间预算触发"
        );
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "执行时间") != NULL,
            "Configure 时间诊断"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-021", "find 深度策略");
    {
        const char *source =
            "list<string> files = find_files(\"*.c\", "
            "{\"tests/data/cmk/find\"});\n"
            "int n = len(files);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "find 深度语义");
        ctx.runtime = cmk_runtime_create(
            ctx.semantic, &ctx.config, ctx.builtins, &ctx.diagnostic
        );
        CC_CHECK(ctx.runtime != NULL, "find 深度运行时创建");
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_find_depth = 1;
        cmk_runtime_set_policy(ctx.runtime, &policy);
        CC_CHECK(
            !cmk_runtime_configure(ctx.runtime),
            "find 深度预算触发"
        );
        const CMKDiagnostic *diag = cmk_test_context_diag(&ctx);
        CC_CHECK(
            diag && diag->message &&
            strstr(diag->message, "find_files") != NULL,
            "find 深度诊断"
        );
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-022", "托管执行栈创建");
    {
        CCBOOL flags[2] = {CCFALSE, CCFALSE};
        CCBOOL result = CCFALSE;
        CC_CHECK(
            cmk_exec_stack_run(
                exec_stack_probe, flags, &result
            ),
            "托管执行栈运行"
        );
        CC_CHECK(flags[0] && result, "托管执行栈回调结果");
        CC_CHECK(flags[1], "托管执行栈余量检查");
    }
    CC_CASE_END();

    CC_CASE("RT-023", "非 return 深递归语句 VM");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    int x = 0;\n"
            "    if (n > 0) { x = f(n - 1) + 1; }\n"
            "    return x;\n"
            "}\n"
            "int g = f(5000);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "深递归赋值语义");
        CC_CHECK(
            cmk_test_context_configure(&ctx),
            "深递归赋值 Configure"
        );
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(get_global(&ctx, "g", &value), "读取深递归结果");
        CC_CHECK(
            value.type == CMK_VALUE_INT &&
            value.as.integer == 5000,
            "深递归赋值返回值"
        );
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RT-024", "循环内非 return 递归语句 VM");
    {
        const char *source =
            "func f(int n) -> int {\n"
            "    int x = 0;\n"
            "    while (n > 0) {\n"
            "        x = f(n - 1) + 1;\n"
            "        break;\n"
            "    }\n"
            "    return x;\n"
            "}\n"
            "int g = f(100);\n"
            "target core() {}\n";
        CMKTestContext ctx;
        cmk_test_context_init(&ctx);
        CC_CHECK(cmk_test_context_load(&ctx, source), "循环递归语义");
        CC_CHECK(
            cmk_test_context_configure(&ctx),
            "循环递归 Configure"
        );
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(get_global(&ctx, "g", &value), "读取循环递归结果");
        CC_CHECK(
            value.type == CMK_VALUE_INT &&
            value.as.integer == 100,
            "循环递归返回值"
        );
        cmk_value_clear(&value);
        cmk_test_context_clear(&ctx);
    }
    CC_CASE_END();
}
