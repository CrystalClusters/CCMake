/*
 * @Description: CMK Program 测试：模块加载、导入、循环检测与 Configure。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "ccvar.h"
#include "ccmake_header.h"
#include "cmk/cmk_program.h"
#include "cmk/cmk_semantic_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct cmk_program_test {
    CCVarTree *tree;
    CMKDiagnostic diagnostic;
    CMKProgram *program;
} CMKProgramTest;

static void program_test_init(CMKProgramTest *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->tree = ccvar_create();
    cmk_diagnostic_init(&ctx->diagnostic);
    if (ctx->tree)
        ctx->program = cmk_program_create(ctx->tree, &ctx->diagnostic);
}

static void program_test_clear(CMKProgramTest *ctx)
{
    if (ctx->program)
        cmk_program_destroy(ctx->program);
    if (ctx->tree)
        ccvar_destroy(ctx->tree);
    cmk_diagnostic_clear(&ctx->diagnostic);
    memset(ctx, 0, sizeof(*ctx));
}

static CCBOOL program_test_load(
    CMKProgramTest *ctx, const char *path
)
{
    if (!ctx->program)
        return CCFALSE;
    return cmk_program_load(ctx->program, path, NULL);
}

static void check_program_error(
    const char *path, const char *expected
)
{
    CMKProgramTest ctx;
    program_test_init(&ctx);
    CC_CHECK(!program_test_load(&ctx, path), "模块加载应失败");
    CC_CHECK(
        ctx.diagnostic.message &&
        strstr(ctx.diagnostic.message, expected) != NULL,
        "模块诊断匹配"
    );
    program_test_clear(&ctx);
}

void test_cmk_program(void)
{


    /* ------------------------------------------------------------------
     * PRG-002 @include 不加载额外 .ccm，但模块可正常配置
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-002", "@include 与配置");
    {
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/cmk/program/include.cmk"
            ),
            "加载 include 模块"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure include 模块");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-003 循环导入
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-003", "循环导入");
    {
        check_program_error(
            "tests/data/cmk/program/cycle_a.cmk", "循环导入"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-004 导入错误
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-004", "导入错误");
    {
        check_program_error(
            "tests/data/cmk/program/missing_func.cmk", "不存在函数"
        );
        check_program_error(
            "tests/data/cmk/program/missing_file.cmk", "无法打开"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-005 通配导入与函数别名
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-005", "通配导入与函数别名");
    {
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/cmk/program/wild_root.cmk"
            ),
            "加载通配导入"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure 通配导入");
        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(
            cmk_program_get_global(ctx.program, "value", 5, &value),
            "读取通配导入结果"
        );
        CC_CHECK(value.as.integer == 6, "通配导入函数结果");
        cmk_value_clear(&value);
        program_test_clear(&ctx);

        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/cmk/program/alias_root.cmk"
            ),
            "加载函数别名"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure 函数别名");
        cmk_value_init(&value);
        CC_CHECK(
            cmk_program_get_global(ctx.program, "value", 5, &value),
            "读取函数别名结果"
        );
        CC_CHECK(value.as.integer == 8, "函数别名结果");
        cmk_value_clear(&value);
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * PRG-006 Program 执行策略
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-006", "Program 执行策略");
    {
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/cmk/program/policy.cmk"
            ),
            "加载策略用例"
        );
        CMKExecutionPolicy policy;
        cmk_execution_policy_init(&policy);
        policy.max_steps = 2;
        cmk_program_set_policy(ctx.program, &policy);
        CC_CHECK(!cmk_program_configure(ctx.program), "Program 步数预算触发");
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
        _putenv("CCMAKE_CMK_MAX_STEPS=2");
#else
        setenv("CCMAKE_CMK_MAX_STEPS", "2", 1);
#endif
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(
                &ctx, "tests/data/cmk/program/policy.cmk"
            ),
            "加载环境策略用例"
        );
        CC_CHECK(
            !cmk_program_configure(ctx.program),
            "环境变量步数预算触发"
        );
        CC_CHECK(
            ctx.diagnostic.message &&
            strstr(ctx.diagnostic.message, "执行步数") != NULL,
            "环境变量策略诊断"
        );
        program_test_clear(&ctx);
#if defined(_WIN32)
        _putenv("CCMAKE_CMK_MAX_STEPS=");
#else
        unsetenv("CCMAKE_CMK_MAX_STEPS");
#endif
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * MAIN/RUN 执行入口与动作链
     * ---------------------------------------------------------------- */
    CC_CASE("MAIN-001", "调用 main 并执行动作");
    {
        remove("out/main_ok");
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/cmk/program/main_ok.cmk"),
            "加载 main_ok"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure main_ok");
        CC_CHECK(cmk_program_run_main(ctx.program), "执行 main");
        CC_CHECK(remove("out/main_ok") == 0, "动作创建目录");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("MAIN-002", "缺少 main 时报错");
    {
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/cmk/program/no_main.cmk"),
            "加载 no_main"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure no_main");
        CC_CHECK(!cmk_program_run_main(ctx.program), "无 main 失败");
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
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/cmk/program/main_args.cmk"),
            "加载 main_args"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure main_args");
        CC_CHECK(!cmk_program_run_main(ctx.program), "拒绝带参数 main");
        program_test_clear(&ctx);

        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/cmk/program/main_return.cmk"),
            "加载 main_return"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure main_return");
        CC_CHECK(!cmk_program_run_main(ctx.program), "拒绝非 void main");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RUN-001", "多个动作按顺序执行");
    {
        remove("out/run_a");
        remove("out/run_b");
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/cmk/program/run_order.cmk"),
            "加载 run_order"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure run_order");
        CC_CHECK(cmk_program_run_main(ctx.program), "执行 run_order");
        CC_CHECK(remove("out/run_a") == 0, "第一个动作");
        CC_CHECK(remove("out/run_b") == 0, "第二个动作");
        program_test_clear(&ctx);
    }
    CC_CASE_END();

    CC_CASE("RUN-002", "动作失败停止后续动作");
    {
        remove("out/run_after_fail");
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/cmk/program/run_stop.cmk"),
            "加载 run_stop"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure run_stop");
        CC_CHECK(!cmk_program_run_main(ctx.program), "失败传播");
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
        _putenv("CCMAKE_CMK_MAX_PROCESSES=7");
        _putenv("CCMAKE_CMK_MAX_OUTPUT_CAPTURE=4096");
#else
        setenv("CCMAKE_CMK_MAX_PROCESSES", "7", 1);
        setenv("CCMAKE_CMK_MAX_OUTPUT_CAPTURE", "4096", 1);
#endif
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CMKExecutionPolicy policy;
        cmk_program_get_policy(ctx.program, &policy);
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
        _putenv("CCMAKE_CMK_MAX_PROCESSES=");
        _putenv("CCMAKE_CMK_MAX_OUTPUT_CAPTURE=");
#else
        unsetenv("CCMAKE_CMK_MAX_PROCESSES");
        unsetenv("CCMAKE_CMK_MAX_OUTPUT_CAPTURE");
#endif
    }
    CC_CASE_END();


}
