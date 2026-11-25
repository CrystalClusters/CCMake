/*
 * @Description: MK Program 测试：模块加载、导入、循环检测与 Configure。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "ccvar.h"
#include "ccmake_header.h"
#include "mk/mk_program.h"
#include "mk/mk_semantic_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct mk_program_test {
    CCVarTree *tree;
    MKDiagnostic diagnostic;
    MKProgram *program;
} MKProgramTest;

static void program_test_init(MKProgramTest *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->tree = ccvar_create();
    mk_diagnostic_init(&ctx->diagnostic);
    if (ctx->tree)
        ctx->program = mk_program_create(ctx->tree, &ctx->diagnostic);
}

static void program_test_clear(MKProgramTest *ctx)
{
    if (ctx->program)
        mk_program_destroy(ctx->program);
    if (ctx->tree)
        ccvar_destroy(ctx->tree);
    mk_diagnostic_clear(&ctx->diagnostic);
    memset(ctx, 0, sizeof(*ctx));
}

static CCBOOL program_test_load(
    MKProgramTest *ctx, const char *path
)
{
    if (!ctx->program)
        return CCFALSE;
    return mk_program_load(ctx->program, path, NULL);
}

static void check_program_error(
    const char *path, const char *expected
)
{
    MKProgramTest ctx;
    program_test_init(&ctx);
    CC_CHECK(!program_test_load(&ctx, path), "模块加载应失败");
    CC_CHECK(
        ctx.diagnostic.message &&
        strstr(ctx.diagnostic.message, expected) != NULL,
        "模块诊断匹配"
    );
    program_test_clear(&ctx);
}

void test_mk_program(void)
{


    /* ------------------------------------------------------------------
     * PRG-002 @include 不加载额外 .mc，但模块可正常配置
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-002", "@include 与配置");
    {
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/mk/program/include.mk"
            ),
            "加载 include 模块"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure include 模块");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-003 循环导入
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-003", "循环导入");
    {
        check_program_error(
            "tests/data/mk/program/cycle_a.mk", "循环导入"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-004 导入错误
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-004", "导入错误");
    {
        check_program_error(
            "tests/data/mk/program/missing_func.mk", "不存在函数"
        );
        check_program_error(
            "tests/data/mk/program/missing_file.mk", "无法打开"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-005 通配导入与函数别名
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-005", "通配导入与函数别名");
    {
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/mk/program/wild_root.mk"
            ),
            "加载通配导入"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure 通配导入");
        MKValue value;
        mk_value_init(&value);
        CC_CHECK(
            mk_program_get_global(ctx.program, "value", 5, &value),
            "读取通配导入结果"
        );
        CC_CHECK(value.as.integer == 6, "通配导入函数结果");
        mk_value_clear(&value);
        program_test_clear(&ctx);

        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/mk/program/alias_root.mk"
            ),
            "加载函数别名"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure 函数别名");
        mk_value_init(&value);
        CC_CHECK(
            mk_program_get_global(ctx.program, "value", 5, &value),
            "读取函数别名结果"
        );
        CC_CHECK(value.as.integer == 8, "函数别名结果");
        mk_value_clear(&value);
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-006 Program 执行策略
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-006", "Program 执行策略");
    {
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/mk/program/policy.mk"
            ),
            "加载策略用例"
        );
        MKExecutionPolicy policy;
        mk_execution_policy_init(&policy);
        policy.max_steps = 2;
        mk_program_set_policy(ctx.program, &policy);
        CC_CHECK(!mk_program_configure(ctx.program), "Program 步数预算触发");
        CC_CHECK(
            ctx.diagnostic.message &&
            strstr(ctx.diagnostic.message, "执行步数") != NULL,
            "Program 策略诊断"
        );
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-007 环境变量执行策略
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-007", "环境变量执行策略");
    {
#if defined(_WIN32)
        _putenv("CCMAKE_MK_MAX_STEPS=2");
#else
        setenv("CCMAKE_MK_MAX_STEPS", "2", 1);
#endif
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/mk/program/policy.mk"
            ),
            "加载环境策略用例"
        );
        CC_CHECK(
            !mk_program_configure(ctx.program),
            "环境变量步数预算触发"
        );
        CC_CHECK(
            ctx.diagnostic.message &&
            strstr(ctx.diagnostic.message, "执行步数") != NULL,
            "环境变量策略诊断"
        );
        program_test_clear(&ctx);
#if defined(_WIN32)
        _putenv("CCMAKE_MK_MAX_STEPS=");
#else
        unsetenv("CCMAKE_MK_MAX_STEPS");
#endif
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * MAIN/RUN 执行入口与动作链
     * ---------------------------------------------------------------- */
    CC_CASE("MAIN-001", "调用 main 并执行动作");
    {
        remove("out/main_ok");
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/mk/program/main_ok.mk"),
            "加载 main_ok"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure main_ok");
        CC_CHECK(mk_program_run_main(ctx.program), "执行 main");
        CC_CHECK(remove("out/main_ok") == 0, "动作创建目录");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("MAIN-002", "缺少 main 时报错");
    {
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/mk/program/no_main.mk"),
            "加载 no_main"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure no_main");
        CC_CHECK(!mk_program_run_main(ctx.program), "无 main 失败");
        CC_CHECK(
            ctx.diagnostic.message &&
            strstr(ctx.diagnostic.message, "缺少入口 main") != NULL,
            "缺少入口诊断"
        );
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("MAIN-003", "main 签名错误");
    {
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/mk/program/main_args.mk"),
            "加载 main_args"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure main_args");
        CC_CHECK(!mk_program_run_main(ctx.program), "拒绝带参数 main");
        program_test_clear(&ctx);

        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/mk/program/main_return.mk"),
            "加载 main_return"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure main_return");
        CC_CHECK(!mk_program_run_main(ctx.program), "拒绝非 void main");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RUN-001", "多个动作按顺序执行");
    {
        remove("out/run_a");
        remove("out/run_b");
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/mk/program/run_order.mk"),
            "加载 run_order"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure run_order");
        CC_CHECK(mk_program_run_main(ctx.program), "执行 run_order");
        CC_CHECK(remove("out/run_a") == 0, "第一个动作");
        CC_CHECK(remove("out/run_b") == 0, "第二个动作");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RUN-002", "动作失败停止后续动作");
    {
        remove("out/run_after_fail");
        MKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/mk/program/run_stop.mk"),
            "加载 run_stop"
        );
        CC_CHECK(mk_program_configure(ctx.program), "Configure run_stop");
        CC_CHECK(!mk_program_run_main(ctx.program), "失败传播");
        CC_CHECK(remove("out/run_after_fail") != 0, "后续动作未执行");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-009 并发/捕获策略环境变量
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-009", "并发/捕获策略环境变量");
    {
#if defined(_WIN32)
        _putenv("CCMAKE_MK_MAX_PROCESSES=7");
        _putenv("CCMAKE_MK_MAX_OUTPUT_CAPTURE=4096");
#else
        setenv("CCMAKE_MK_MAX_PROCESSES", "7", 1);
        setenv("CCMAKE_MK_MAX_OUTPUT_CAPTURE", "4096", 1);
#endif
        MKProgramTest ctx;
        program_test_init(&ctx);
        MKExecutionPolicy policy;
        mk_program_get_policy(ctx.program, &policy);
        CC_CHECK_EQ_I64(
            "max_processes 环境变量",
            7, (CCINT64)policy.max_processes
        );
        CC_CHECK_EQ_I64(
            "max_output_capture 环境变量",
            4096, (CCINT64)policy.max_output_capture
        );
        program_test_clear(&ctx);
#if defined(_WIN32)
        _putenv("CCMAKE_MK_MAX_PROCESSES=");
        _putenv("CCMAKE_MK_MAX_OUTPUT_CAPTURE=");
#else
        unsetenv("CCMAKE_MK_MAX_PROCESSES");
        unsetenv("CCMAKE_MK_MAX_OUTPUT_CAPTURE");
#endif
    }
    CC_CASE_END();


}
