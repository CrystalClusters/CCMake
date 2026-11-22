/*
 * @Description: CMK Scheduler 状态机测试（M6，fake Executor）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_scheduler.h"

#include <string.h>

typedef struct fake_executor {
    const char *fail_name;
    CCUINT32 count;
    char names[16][32];
} FakeExecutor;

static CCBOOL fake_run(
    void *user, const CMKPlanTarget *target, CMKExecutorResult *result
)
{
    FakeExecutor *fake = (FakeExecutor *)user;
    if (fake->count < 16)
    {
        CCUINT32 len = target->key.name.len < 31
            ? target->key.name.len : 31;
        memcpy(fake->names[fake->count], target->key.name.data, len);
        fake->names[fake->count][len] = '\0';
    }
    __sync_fetch_and_add(&fake->count, 1);
    result->success =
        (fake->fail_name &&
         target->key.name.len == strlen(fake->fail_name) &&
         memcmp(target->key.name.data, fake->fail_name,
                target->key.name.len) == 0)
        ? CCFALSE : CCTRUE;
    result->exit_code = result->success ? 0 : 1;
    result->message = result->success ? NULL : "fake failure";
    return CCTRUE;
}

static void fake_cancel(void *user, CMKTargetId target)
{
    (void)user;
    (void)target;
}

typedef struct fake_cache {
    const char *skip_name;
    CCUINT32 records;
} FakeCache;

static CCBOOL fake_cache_should_skip(
    void *user, const CMKPlanTarget *target, CCBOOL *skip
)
{
    FakeCache *cache = (FakeCache *)user;
    *skip = (cache->skip_name &&
             target->key.name.len == strlen(cache->skip_name) &&
             memcmp(target->key.name.data, cache->skip_name,
                    target->key.name.len) == 0)
        ? CCTRUE : CCFALSE;
    return CCTRUE;
}

static CCBOOL fake_cache_record(
    void *user, const CMKPlanTarget *target
)
{
    (void)target;
    FakeCache *cache = (FakeCache *)user;
    cache->records++;
    return CCTRUE;
}

static CCBOOL sched_build_plan(
    CMKPlan **out, CMKTargetInfo *info_a,
    CMKTargetInfo *info_b, CMKTargetInfo *info_c
)
{
    CMKPlan *plan = cmk_plan_create();
    if (!plan)
        return CCFALSE;
    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);

    cmk_string_set(&info_a->name, "A", 1);
    cmk_string_set(&info_b->name, "B", 1);
    cmk_string_set(&info_c->name, "C", 1);

    CMKTargetDraft a;
    CMKTargetDraft b;
    CMKTargetDraft c;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    a.info = info_a;
    b.info = info_b;
    c.info = info_c;
    cmk_value_set_list(&a.depends_on, CMK_VALUE_TARGET_REF);
    cmk_value_set_list(&b.depends_on, CMK_VALUE_TARGET_REF);
    cmk_value_set_list(&c.depends_on, CMK_VALUE_TARGET_REF);
    cmk_value_set_list(&a.inputs, CMK_VALUE_STRING);
    cmk_value_set_list(&b.inputs, CMK_VALUE_STRING);
    cmk_value_set_list(&c.inputs, CMK_VALUE_STRING);
    cmk_value_set_list(&a.outputs, CMK_VALUE_STRING);
    cmk_value_set_list(&b.outputs, CMK_VALUE_STRING);
    cmk_value_set_list(&c.outputs, CMK_VALUE_STRING);
    cmk_value_set_list(&a.actions, CMK_VALUE_ACTION);
    cmk_value_set_list(&b.actions, CMK_VALUE_ACTION);
    cmk_value_set_list(&c.actions, CMK_VALUE_ACTION);

    CMKValue dep;
    cmk_value_init(&dep);
    cmk_value_set_target_ref(&dep, "m", 1, "B", 1);
    cmk_list_append(a.depends_on.as.list, &dep);
    cmk_value_clear(&dep);
    cmk_value_set_target_ref(&dep, "m", 1, "C", 1);
    cmk_list_append(a.depends_on.as.list, &dep);
    cmk_value_clear(&dep);

    CMKValue action;
    cmk_value_init(&action);
    cmk_value_set_action(&action, CMK_ACTION_EXEC);
    cmk_string_set(&action.as.action->program, "b", 1);
    cmk_list_append(b.actions.as.list, &action);
    cmk_value_clear(&action);
    cmk_value_set_action(&action, CMK_ACTION_EXEC);
    cmk_string_set(&action.as.action->program, "c", 1);
    cmk_list_append(c.actions.as.list, &action);
    cmk_value_clear(&action);

    CCBOOL ok =
        cmk_plan_add_draft(plan, "m", 1, &a, &diagnostic) &&
        cmk_plan_add_draft(plan, "m", 1, &b, &diagnostic) &&
        cmk_plan_add_draft(plan, "m", 1, &c, &diagnostic) &&
        cmk_plan_freeze(plan, "m", 1, "A", 1, &diagnostic);

    cmk_value_clear(&a.depends_on);
    cmk_value_clear(&a.inputs);
    cmk_value_clear(&a.outputs);
    cmk_value_clear(&a.actions);
    cmk_value_clear(&b.depends_on);
    cmk_value_clear(&b.inputs);
    cmk_value_clear(&b.outputs);
    cmk_value_clear(&b.actions);
    cmk_value_clear(&c.depends_on);
    cmk_value_clear(&c.inputs);
    cmk_value_clear(&c.outputs);
    cmk_value_clear(&c.actions);
    cmk_diagnostic_clear(&diagnostic);
    if (!ok)
    {
        cmk_plan_destroy(plan);
        return CCFALSE;
    }
    *out = plan;
    return CCTRUE;
}

static void sched_case_success(void)
{
    CC_CASE("SCH-001", "依赖成功与聚合目标");
    CMKPlan *plan = NULL;
    CMKTargetInfo a, b, c;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    CC_CHECK(sched_build_plan(&plan, &a, &b, &c), "构建计划");
    FakeExecutor fake;
    memset(&fake, 0, sizeof(fake));
    CMKExecutorOps ops;
    ops.run = fake_run;
    ops.cancel = fake_cancel;
    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKScheduler *sched = cmk_scheduler_create(
        plan, &ops, &fake, &diagnostic
    );
    CC_CHECK(sched != NULL, "Scheduler 创建");
    CC_CHECK(cmk_scheduler_run(sched, CCFALSE), "Scheduler 成功");
    CC_CHECK_EQ_I64("执行次数", 2, (CCINT64)fake.count);
    CC_CHECK_EQ_STR("第一个目标", "B", fake.names[0]);
    CC_CHECK_EQ_STR("第二个目标", "C", fake.names[1]);
    CC_CHECK(
        cmk_scheduler_target_state(sched, 0) == CMK_TARGET_SUCCESS,
        "聚合目标成功"
    );
    cmk_scheduler_destroy(sched);
    cmk_plan_destroy(plan);
    cmk_diagnostic_clear(&diagnostic);
    cmk_string_clear(&a.name);
    cmk_string_clear(&b.name);
    cmk_string_clear(&c.name);
    CC_CASE_END();
}

static void sched_case_failure(CCBOOL keep_going)
{
    CC_CASE(keep_going ? "SCH-003" : "SCH-002",
            keep_going ? "keep-going" : "fail-fast");
    CMKPlan *plan = NULL;
    CMKTargetInfo a, b, c;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    CC_CHECK(sched_build_plan(&plan, &a, &b, &c), "构建计划");
    FakeExecutor fake;
    memset(&fake, 0, sizeof(fake));
    fake.fail_name = "B";
    CMKExecutorOps ops;
    ops.run = fake_run;
    ops.cancel = fake_cancel;
    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKScheduler *sched = cmk_scheduler_create(
        plan, &ops, &fake, &diagnostic
    );
    CC_CHECK(sched != NULL, "Scheduler 创建");
    CC_CHECK(!cmk_scheduler_run(sched, keep_going), "根目标失败");
    CC_CHECK(
        cmk_scheduler_target_state(sched, 1) == CMK_TARGET_FAILED,
        "B 失败"
    );
    CC_CHECK(
        cmk_scheduler_target_state(sched, 0) == CMK_TARGET_BLOCKED,
        "A 阻塞"
    );
    if (keep_going)
    {
        CC_CHECK_EQ_I64("执行 B/C", 2, (CCINT64)fake.count);
        CC_CHECK(
            cmk_scheduler_target_state(sched, 2) ==
                CMK_TARGET_SUCCESS,
            "C 独立成功"
        );
    }
    else
    {
        CC_CHECK(
            cmk_scheduler_target_state(sched, 2) ==
                CMK_TARGET_CANCELLED,
            "C 被取消"
        );
    }
    cmk_scheduler_destroy(sched);
    cmk_plan_destroy(plan);
    cmk_diagnostic_clear(&diagnostic);
    cmk_string_clear(&a.name);
    cmk_string_clear(&b.name);
    cmk_string_clear(&c.name);
    CC_CASE_END();
}

static void sched_case_parallel(void)
{
    CC_CASE("SCH-004", "并发 READY 批次");
    CMKPlan *plan = NULL;
    CMKTargetInfo a, b, c;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    CC_CHECK(sched_build_plan(&plan, &a, &b, &c), "构建计划");
    FakeExecutor fake;
    memset(&fake, 0, sizeof(fake));
    CMKExecutorOps ops;
    ops.run = fake_run;
    ops.cancel = fake_cancel;
    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKScheduler *sched = cmk_scheduler_create(
        plan, &ops, &fake, &diagnostic
    );
    CC_CHECK(sched != NULL, "Scheduler 创建");
    cmk_scheduler_set_jobs(sched, 2);
    CC_CHECK(cmk_scheduler_run(sched, CCFALSE), "并发调度成功");
    CC_CHECK_EQ_I64("并发执行次数", 2, (CCINT64)fake.count);
    cmk_scheduler_destroy(sched);
    cmk_plan_destroy(plan);
    cmk_diagnostic_clear(&diagnostic);
    cmk_string_clear(&a.name);
    cmk_string_clear(&b.name);
    cmk_string_clear(&c.name);
    CC_CASE_END();
}

static void sched_case_cache(void)
{
    CC_CASE("SCH-005", "缓存 SKIPPED");
    CMKPlan *plan = NULL;
    CMKTargetInfo a, b, c;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    CC_CHECK(sched_build_plan(&plan, &a, &b, &c), "构建计划");
    FakeExecutor fake;
    memset(&fake, 0, sizeof(fake));
    FakeCache cache;
    memset(&cache, 0, sizeof(cache));
    cache.skip_name = "B";
    CMKExecutorOps ops;
    ops.run = fake_run;
    ops.cancel = fake_cancel;
    CMKCacheOps cache_ops;
    cache_ops.should_skip = fake_cache_should_skip;
    cache_ops.record = fake_cache_record;
    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKScheduler *sched = cmk_scheduler_create(
        plan, &ops, &fake, &diagnostic
    );
    CC_CHECK(sched != NULL, "Scheduler 创建");
    cmk_scheduler_set_cache(sched, &cache_ops, &cache);
    CC_CHECK(cmk_scheduler_run(sched, CCFALSE), "缓存调度成功");
    CC_CHECK(
        cmk_scheduler_target_state(sched, 1) == CMK_TARGET_SKIPPED,
        "B 缓存跳过"
    );
    CC_CHECK_EQ_I64("执行次数", 1, (CCINT64)fake.count);
    CC_CHECK(
        cmk_scheduler_target_state(sched, 2) == CMK_TARGET_SUCCESS,
        "C 执行成功"
    );
    CC_CHECK_EQ_I64("缓存记录次数", 1, (CCINT64)cache.records);
    cmk_scheduler_destroy(sched);
    cmk_plan_destroy(plan);
    cmk_diagnostic_clear(&diagnostic);
    cmk_string_clear(&a.name);
    cmk_string_clear(&b.name);
    cmk_string_clear(&c.name);
    CC_CASE_END();
}

void test_cmk_scheduler(void)
{
    sched_case_success();
    sched_case_failure(CCFALSE);
    sched_case_failure(CCTRUE);
    sched_case_parallel();
    sched_case_cache();
}
