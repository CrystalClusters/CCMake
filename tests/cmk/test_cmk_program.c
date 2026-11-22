/*
 * @Description: CMK Program 测试：模块加载、导入、循环检测与 Configure。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "ccvar.h"
#include "cmk/cmk_program.h"

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

static const CMKTargetDraft *find_target(
    const CMKProgramTest *ctx, const char *name
)
{
    CCUINT32 count = cmk_program_target_count(ctx->program);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKTargetDraft *target =
            cmk_program_target_at(ctx->program, i);
        if (target && target->info &&
            target->info->name.len == strlen(name) &&
            memcmp(target->info->name.data, name, target->info->name.len) == 0)
            return target;
    }
    return NULL;
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
     * PRG-001 导入函数、导入 target 与 Configure
     * ---------------------------------------------------------------- */
    CC_CASE("PRG-001", "导入函数、导入 target 与 Configure");
    {
        CMKProgramTest ctx;
        program_test_init(&ctx);
        CC_CHECK(
            program_test_load(&ctx, "tests/data/cmk/program/root.cmk"),
            "加载根模块"
        );
        CC_CHECK(cmk_program_configure(ctx.program), "Configure 根模块");
        CC_CHECK_EQ_I64(
            "模块数", 2, (CCINT64)cmk_program_module_count(ctx.program)
        );

        CMKValue value;
        cmk_value_init(&value);
        CC_CHECK(
            cmk_program_get_global(ctx.program, "value", 5, &value),
            "读取跨模块函数结果"
        );
        CC_CHECK(value.type == CMK_VALUE_INT && value.as.integer == 42,
                 "跨模块函数值");
        cmk_value_clear(&value);

        CC_CHECK_EQ_I64(
            "根 target 数", 1,
            (CCINT64)cmk_program_target_count(ctx.program)
        );
        const CMKTargetDraft *main_target = find_target(&ctx, "main");
        CC_CHECK(main_target != NULL, "找到 main");
        if (main_target)
        {
            CC_CHECK_EQ_I64(
                "main depends_on 数", 1,
                (CCINT64)cmk_list_count(main_target->depends_on.as.list)
            );
            const CMKValue *dep =
                cmk_list_at(main_target->depends_on.as.list, 0);
            CC_CHECK(
                dep && dep->type == CMK_VALUE_TARGET_REF &&
                strcmp(dep->as.target_ref.name.data, "core") == 0,
                "导入 target 引用"
            );
            const CMKValue *output =
                cmk_list_at(main_target->outputs.as.list, 0);
            CC_CHECK_EQ_STR("main 输出", "main.out", output->as.string.data);
        }
        program_test_clear(&ctx);
    }
    CC_CASE_END();

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
        const CMKTargetDraft *main_target = find_target(&ctx, "main");
        CC_CHECK(main_target != NULL, "include main");
        if (main_target)
        {
            const CMKValue *output =
                cmk_list_at(main_target->outputs.as.list, 0);
            CC_CHECK_EQ_STR(
                "include 目标输出", "configured", output->as.string.data
            );
        }
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
            "tests/data/cmk/program/missing_target.cmk", "不存在 target"
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
}
