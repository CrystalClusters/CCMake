/*
 * @Description: CCMake 应用层构建驱动
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "build.h"

#include "ccmake_header.h"
#include "ccvar.h"
#include "cmk/cmk_cache.h"
#include "cmk/cmk_executor.h"
#include "cmk/cmk_program.h"
#include "cmk/cmk_scheduler.h"

#include <color_print.h>

typedef struct program_cache_ctx {
    CMKCache *cache;
    CMKPlan *plan;
} ProgramCacheCtx;

static CMKTargetId cache_target_id(
    const ProgramCacheCtx *ctx, const CMKPlanTarget *target
)
{
    CCUINT32 module_len = 0;
    CCUINT32 name_len = 0;
    const char *module = cmk_plan_target_module(target, &module_len);
    const char *name = cmk_plan_target_name(target, &name_len);
    return cmk_plan_find_target(
        ctx->plan, module, module_len, name, name_len
    );
}

static CCBOOL program_cache_should_skip(
    void *user, const CMKPlanTarget *target, CCBOOL *skip
)
{
    ProgramCacheCtx *ctx = (ProgramCacheCtx *)user;
    CMKTargetId id = cache_target_id(ctx, target);
    return cmk_cache_should_skip(
        ctx->cache, ctx->plan, id, CCFALSE, skip
    );
}

static CCBOOL program_cache_record(
    void *user, const CMKPlanTarget *target
)
{
    ProgramCacheCtx *ctx = (ProgramCacheCtx *)user;
    CMKTargetId id = cache_target_id(ctx, target);
    return cmk_cache_record(ctx->cache, ctx->plan, id);
}

CCBOOL build_project(void)
{
    if (!ccmake_global_var.cmk_path)
        return CCTRUE;
    // --dump-config 只导出 .ccm，不执行 .cmk Configure
    if (ccmake_global_var.action == CC_CMD_ACTION_DUMP_CONFIG)
        return CCTRUE;

    CCVarTree *tree = ccvar_global();
    if (!tree)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "构建脚本解析失败：全局变量树未初始化\n"
        );
        return CCFALSE;
    }

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKProgram *program = cmk_program_create(tree, &diagnostic);
    if (!program)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "构建脚本解析失败：程序上下文初始化失败\n"
        );
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    if (ccmake_global_var.target)
        cmk_program_set_target_name(
            program, ccmake_global_var.target
        );
    CCBOOL ok = cmk_program_load(
        program, ccmake_global_var.cmk_path,
        ccmake_global_var.work_dir
    );
    if (ok)
        ok = cmk_program_configure(program);
    if (!ok)
    {
        cmk_diagnostic_print(&diagnostic, "构建脚本");
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }
    if (!cmk_program_lower(
            program,
            ccmake_global_var.target
                ? ccmake_global_var.target : "main"
        ))
    {
        cmk_diagnostic_print(&diagnostic, "构建计划");
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    if (ccmake_global_var.action == CC_CMD_ACTION_LIST_TARGETS)
    {
        color_print("可用目标：\n");
        const CMKPlan *plan = cmk_program_plan(program);
        CCUINT32 count = plan ? cmk_plan_target_count(plan) : 0;
        for (CCUINT32 i = 0; i < count; i++)
        {
            const CMKPlanTarget *target = cmk_plan_target_at(plan, i);
            if (target)
                color_print(
                    "  %s\n", cmk_plan_target_name(target, NULL)
                );
        }
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCTRUE;
    }

    CMKExecutionPolicy policy;
    cmk_program_get_policy(program, &policy);
    CMKPlan *plan = cmk_program_take_plan(program);
    if (!plan)
    {
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }
    CMKExecutor *executor = cmk_executor_create(
        &policy, &diagnostic
    );
    if (!executor)
    {
        cmk_plan_destroy(plan);
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }
    CMKScheduler *scheduler = cmk_scheduler_create(
        plan, cmk_executor_ops(), executor, &diagnostic
    );
    if (!scheduler)
    {
        cmk_executor_destroy(executor);
        cmk_plan_destroy(plan);
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }
    cmk_scheduler_set_jobs(
        scheduler,
        policy.max_processes != 0
            ? policy.max_processes : ccmake_global_var.jobs
    );
    CMKCache *cache = cmk_cache_create(
        ".ccmake-cache", &diagnostic
    );
    ProgramCacheCtx cache_ctx;
    cache_ctx.cache = cache;
    cache_ctx.plan = plan;
    CMKCacheOps cache_ops;
    cache_ops.should_skip = program_cache_should_skip;
    cache_ops.record = program_cache_record;
    if (cache)
    {
        cmk_scheduler_set_cache(
            scheduler, &cache_ops, &cache_ctx
        );
        cmk_scheduler_set_force(
            scheduler, ccmake_global_var.always_build
        );
    }
    ok = cmk_scheduler_run(
        scheduler, ccmake_global_var.keep_going
    );
    if (!ok)
        cmk_diagnostic_print(&diagnostic, "构建");
    cmk_scheduler_destroy(scheduler);
    cmk_executor_destroy(executor);
    if (cache)
        cmk_cache_destroy(cache);
    cmk_plan_destroy(plan);

    cmk_program_destroy(program);
    cmk_diagnostic_clear(&diagnostic);
    return ok;
}
