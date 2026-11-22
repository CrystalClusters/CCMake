/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 单线程 Scheduler（M6）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_scheduler.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdio.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#endif

#define CMK_SCHED_COMMENT CC_DBG_REL_TAG(".cmk 调度", NULL)

struct cmk_scheduler {
    CMKPlan *plan;
    const CMKExecutorOps *ops;
    void *user;
    CMKDiagnostic *diagnostic;
    CCBOOL *closure;
    CMKTargetId *order;
    CCUINT32 order_count;
    const char **messages;
    CCUINT32 count;
    CCUINT32 executed;
    CCUINT32 failed;
    CCUINT32 jobs;
    const CMKCacheOps *cache;
    void *cache_user;
    CCBOOL force;
};

#if 0
static CCBOOL cmk_sched_error(
    CMKScheduler *scheduler, CMKTargetId id,
    const char *fmt, ...
)
{
    if (!scheduler || !scheduler->diagnostic)
        return CCFALSE;
    va_list ap;
    va_start(ap, fmt);
    CCBOOL ok = cmk_diagnostic_vset(
        scheduler->diagnostic, CMK_DIAG_ERROR,
        id < scheduler->count
            ? cmk_plan_target_at(scheduler->plan, id)->key.module.data
            : NULL,
        0, 0, fmt, ap
    );
    va_end(ap);
    return ok ? CCFALSE : CCFALSE;
}
#endif

CMKScheduler *cmk_scheduler_create(
    CMKPlan *plan,
    const CMKExecutorOps *ops, void *user,
    CMKDiagnostic *diagnostic
)
{
    if (!plan || !cmk_plan_is_frozen(plan) ||
        cmk_plan_root(plan) == CMK_TARGET_ID_NIL ||
        !ops || !ops->run)
        return NULL;

    CMKScheduler *scheduler = ccalloc(
        NULL, sizeof(CMKScheduler), CMK_SCHED_COMMENT
    );
    if (!scheduler)
        return NULL;
    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->plan = plan;
    scheduler->ops = ops;
    scheduler->user = user;
    scheduler->diagnostic = diagnostic;
    scheduler->count = cmk_plan_target_count(plan);
    scheduler->jobs = 1;

    if (scheduler->count > 0)
    {
        scheduler->closure = ccalloc(
            NULL, (CCUINT64)scheduler->count * sizeof(CCBOOL),
            CMK_SCHED_COMMENT
        );
        scheduler->order_count = cmk_plan_order_count(plan);
        if (scheduler->order_count > 0)
        {
            scheduler->order = ccalloc(
                NULL,
                (CCUINT64)scheduler->order_count * sizeof(CMKTargetId),
                CMK_SCHED_COMMENT
            );
            if (scheduler->order)
            {
                for (CCUINT32 i = 0; i < scheduler->order_count; i++)
                    scheduler->order[i] = cmk_plan_order_at(plan, i);
            }
        }
        scheduler->messages = ccalloc(
            NULL, (CCUINT64)scheduler->count * sizeof(char *),
            CMK_SCHED_COMMENT
        );
        if (!scheduler->closure || !scheduler->messages ||
            (scheduler->order_count > 0 && !scheduler->order))
        {
            cmk_scheduler_destroy(scheduler);
            return NULL;
        }
        memset(
            scheduler->closure, 0,
            (size_t)scheduler->count * sizeof(CCBOOL)
        );
        memset(
            scheduler->messages, 0,
            (size_t)scheduler->count * sizeof(char *)
        );
        for (CCUINT32 i = 0; i < scheduler->count; i++)
        {
            cmk_plan_set_target_state(plan, i, CMK_TARGET_PENDING);
        }
    }
    return scheduler;
}

void cmk_scheduler_destroy(CMKScheduler *scheduler)
{
    if (!scheduler)
        return;
    if (scheduler->messages)
        ccalloc(scheduler->messages, 0, NULL);
    if (scheduler->closure)
        ccalloc(scheduler->closure, 0, NULL);
    if (scheduler->order)
        ccalloc(scheduler->order, 0, NULL);
    ccalloc(scheduler, 0, NULL);
}

static CCBOOL cmk_sched_is_reachable(
    const CMKScheduler *scheduler, CMKTargetId id
)
{
    return (scheduler && id < scheduler->count &&
            scheduler->closure[id]) ? CCTRUE : CCFALSE;
}

static void cmk_sched_mark_reachable(CMKScheduler *scheduler)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CMKTargetId root = cmk_plan_root(scheduler->plan);
    if (!cmk_dyn_push(
            &stack, (CCUINT32)sizeof(CMKTargetId),
            (CCINT32)sizeof(CMKTargetId), &root
        ))
        return;
    while (stack.size >= (CCUINT32)sizeof(CMKTargetId))
    {
        CMKTargetId id = CMK_TARGET_ID_NIL;
        if (cc_dyn_core_pop(
                &stack, (CCUINT32)sizeof(CMKTargetId),
                (CCINT32)sizeof(CMKTargetId), &id
            ) != 0)
            break;
        if (id >= scheduler->count || scheduler->closure[id])
            continue;
        scheduler->closure[id] = CCTRUE;
        const CMKPlanTarget *target =
            cmk_plan_target_at(scheduler->plan, id);
        CMKTargetId *deps =
            (CMKTargetId *)target->dependencies.array;
        CCUINT32 dep_count = target->dependencies.size /
            (CCUINT32)sizeof(CMKTargetId);
        for (CCUINT32 d = 0; d < dep_count; d++)
        {
            if (!cmk_dyn_push(
                    &stack, (CCUINT32)sizeof(CMKTargetId),
                    (CCINT32)sizeof(CMKTargetId), &deps[d]
                ))
                break;
        }
    }
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
}

static CCBOOL cmk_sched_dep_terminal_bad(
    const CMKScheduler *scheduler, CMKTargetId id
)
{
    const CMKPlanTarget *target =
        cmk_plan_target_at(scheduler->plan, id);
    CMKTargetId *deps = (CMKTargetId *)target->dependencies.array;
    CCUINT32 dep_count = target->dependencies.size /
        (CCUINT32)sizeof(CMKTargetId);
    for (CCUINT32 d = 0; d < dep_count; d++)
    {
        CMKTargetState state =
            cmk_plan_target_at(scheduler->plan, deps[d])->state;
        if (state == CMK_TARGET_FAILED ||
            state == CMK_TARGET_CANCELLED ||
            state == CMK_TARGET_BLOCKED)
            return CCTRUE;
    }
    return CCFALSE;
}

static CCBOOL cmk_sched_deps_success(
    const CMKScheduler *scheduler, CMKTargetId id
)
{
    const CMKPlanTarget *target =
        cmk_plan_target_at(scheduler->plan, id);
    CMKTargetId *deps = (CMKTargetId *)target->dependencies.array;
    CCUINT32 dep_count = target->dependencies.size /
        (CCUINT32)sizeof(CMKTargetId);
    for (CCUINT32 d = 0; d < dep_count; d++)
    {
        CMKTargetState state =
            cmk_plan_target_at(scheduler->plan, deps[d])->state;
        if (state != CMK_TARGET_SUCCESS &&
            state != CMK_TARGET_SKIPPED)
            return CCFALSE;
    }
    return CCTRUE;
}

static void cmk_sched_set_message(
    CMKScheduler *scheduler, CMKTargetId id, const char *message
)
{
    if (!scheduler || id >= scheduler->count || !message)
        return;
    scheduler->messages[id] = message;
}

static CCBOOL __attribute__((unused)) cmk_sched_all_terminal(const CMKScheduler *scheduler)
{
    for (CCUINT32 i = 0; i < scheduler->count; i++)
    {
        if (!cmk_sched_is_reachable(scheduler, i))
            continue;
        CMKTargetState state =
            cmk_plan_target_at(scheduler->plan, i)->state;
        if (state == CMK_TARGET_PENDING ||
            state == CMK_TARGET_READY ||
            state == CMK_TARGET_RUNNING)
            return CCFALSE;
    }
    return CCTRUE;
}

#if 0
static CCBOOL cmk_scheduler_run_legacy(CMKScheduler *scheduler, CCBOOL keep_going)
{
    if (!scheduler)
        return CCFALSE;
    cmk_sched_mark_reachable(scheduler);
    CMKTargetId root = cmk_plan_root(scheduler->plan);

    for (;;)
    {
        /* 依赖失败传播 */
        for (CCUINT32 i = 0; i < scheduler->count; i++)
        {
            if (!cmk_sched_is_reachable(scheduler, i))
                continue;
            const CMKPlanTarget *target =
                cmk_plan_target_at(scheduler->plan, i);
            if (target->state == CMK_TARGET_PENDING &&
                cmk_sched_dep_terminal_bad(scheduler, i))
            {
                cmk_plan_set_target_state(
                    scheduler->plan, i, CMK_TARGET_BLOCKED
                );
                cmk_sched_set_message(
                    scheduler, i, "依赖失败或取消"
                );
            }
        }

        if (!keep_going)
        {
            CCBOOL failed = CCFALSE;
            for (CCUINT32 i = 0; i < scheduler->count; i++)
            {
                if (cmk_sched_is_reachable(scheduler, i) &&
                    cmk_plan_target_at(scheduler->plan, i)->state ==
                        CMK_TARGET_FAILED)
                {
                    failed = CCTRUE;
                    break;
                }
            }
            if (failed)
            {
                for (CCUINT32 i = 0; i < scheduler->count; i++)
                {
                    if (!cmk_sched_is_reachable(scheduler, i))
                        continue;
                    CMKTargetState state =
                        cmk_plan_target_at(scheduler->plan, i)->state;
                    if (state == CMK_TARGET_PENDING ||
                        state == CMK_TARGET_READY)
                        cmk_plan_set_target_state(
                            scheduler->plan, i, CMK_TARGET_CANCELLED
                        );
                }
                break;
            }
        }

        /* 依赖全部成功后进入 READY */
        CCBOOL ready_found = CCFALSE;
        for (CCUINT32 i = 0; i < scheduler->count; i++)
        {
            if (!cmk_sched_is_reachable(scheduler, i))
                continue;
            const CMKPlanTarget *target =
                cmk_plan_target_at(scheduler->plan, i);
            if (target->state == CMK_TARGET_PENDING &&
                cmk_sched_deps_success(scheduler, i))
            {
                cmk_plan_set_target_state(
                    scheduler->plan, i, CMK_TARGET_READY
                );
                ready_found = CCTRUE;
            }
        }
        (void)ready_found;

        CCBOOL executed_any = CCFALSE;
        for (CCUINT32 i = 0; i < scheduler->count; i++)
        {
            if (!cmk_sched_is_reachable(scheduler, i))
                continue;
            const CMKPlanTarget *target =
                cmk_plan_target_at(scheduler->plan, i);
            if (target->state != CMK_TARGET_READY)
                continue;

            if (target->actions.size == 0)
            {
                cmk_plan_set_target_state(
                    scheduler->plan, i, CMK_TARGET_SUCCESS
                );
                executed_any = CCTRUE;
                continue;
            }

            if (scheduler->cache && !scheduler->force &&
                scheduler->cache->should_skip)
            {
                CCBOOL skip = CCFALSE;
                if (scheduler->cache->should_skip(
                        scheduler->cache_user, target, &skip
                    ) && skip)
                {
                    cmk_plan_set_target_state(
                        scheduler->plan, i, CMK_TARGET_SKIPPED
                    );
                    executed_any = CCTRUE;
                    continue;
                }
            }

            cmk_plan_set_target_state(
                scheduler->plan, i, CMK_TARGET_RUNNING
            );
            CMKExecutorResult result;
            memset(&result, 0, sizeof(result));
            CCBOOL ok = scheduler->ops->run(
                scheduler->user, target, &result
            );
            scheduler->executed++;
            if (ok && result.success)
            {
                cmk_plan_set_target_state(
                    scheduler->plan, i, CMK_TARGET_SUCCESS
                );
                if (scheduler->cache && scheduler->cache->record)
                    (void)scheduler->cache->record(
                        scheduler->cache_user, target
                    );
            }
            else
            {
                scheduler->failed++;
                cmk_plan_set_target_state(
                    scheduler->plan, i, CMK_TARGET_FAILED
                );
                cmk_sched_set_message(
                    scheduler, i,
                    result.message ? result.message : "目标执行失败"
                );
            }
            if (result.output.len > 0)
            {
                fwrite(
                    result.output.data, 1,
                    result.output.len, stdout
                );
                fflush(stdout);
            }
            cmk_executor_result_clear(&result);
            executed_any = CCTRUE;
            break;
        }

        if (cmk_sched_all_terminal(scheduler))
            break;
        if (!executed_any)
        {
            cmk_sched_error(
                scheduler, root, "调度无法继续：存在未满足的依赖"
            );
            break;
        }
    }

    return cmk_plan_target_at(scheduler->plan, root)->state ==
        CMK_TARGET_SUCCESS ? CCTRUE : CCFALSE;
}
#endif

static CCBOOL cmk_scheduler_run_serial(CMKScheduler *scheduler, CCBOOL keep_going)
{
    if (!scheduler)
        return CCFALSE;
    cmk_sched_mark_reachable(scheduler);
    CMKTargetId root = cmk_plan_root(scheduler->plan);

    CCBOOL triggered = CCFALSE;
    CCUINT32 order_count = scheduler->order_count;
    for (CCUINT32 o = 0; o < order_count; o++)
    {
        CMKTargetId id = scheduler->order[o];
        if (id >= scheduler->count ||
            !cmk_sched_is_reachable(scheduler, id))
            continue;
        const CMKPlanTarget *target =
            cmk_plan_target_at(scheduler->plan, id);

        if (cmk_sched_dep_terminal_bad(scheduler, id))
        {
            cmk_plan_set_target_state(
                scheduler->plan, id, CMK_TARGET_BLOCKED
            );
            cmk_sched_set_message(
                scheduler, id, "依赖失败或取消"
            );
            continue;
        }
        if (triggered)
        {
            cmk_plan_set_target_state(
                scheduler->plan, id, CMK_TARGET_CANCELLED
            );
            continue;
        }
        if (!cmk_sched_deps_success(scheduler, id))
        {
            cmk_plan_set_target_state(
                scheduler->plan, id, CMK_TARGET_BLOCKED
            );
            cmk_sched_set_message(scheduler, id, "依赖未成功");
            continue;
        }

        cmk_plan_set_target_state(
            scheduler->plan, id, CMK_TARGET_READY
        );
        if (target->actions.size == 0)
        {
            cmk_plan_set_target_state(
                scheduler->plan, id, CMK_TARGET_SUCCESS
            );
            continue;
        }

        if (scheduler->cache && !scheduler->force &&
            scheduler->cache->should_skip)
        {
            CCBOOL skip = CCFALSE;
            if (scheduler->cache->should_skip(
                    scheduler->cache_user, target, &skip
                ) && skip)
            {
                cmk_plan_set_target_state(
                    scheduler->plan, id, CMK_TARGET_SKIPPED
                );
                continue;
            }
        }

        cmk_plan_set_target_state(
            scheduler->plan, id, CMK_TARGET_RUNNING
        );
        CMKExecutorResult result;
        memset(&result, 0, sizeof(result));
        CCBOOL ok = scheduler->ops->run(
            scheduler->user, target, &result
        );
        scheduler->executed++;
        if (ok && result.success)
        {
            cmk_plan_set_target_state(
                scheduler->plan, id, CMK_TARGET_SUCCESS
            );
            if (scheduler->cache && scheduler->cache->record)
                (void)scheduler->cache->record(
                    scheduler->cache_user, target
                );
        }
        else
        {
            scheduler->failed++;
            cmk_plan_set_target_state(
                scheduler->plan, id, CMK_TARGET_FAILED
            );
            cmk_sched_set_message(
                scheduler, id,
                result.message ? result.message : "目标执行失败"
            );
            if (!keep_going)
                triggered = CCTRUE;
        }
    }

    return cmk_plan_target_at(scheduler->plan, root)->state ==
        CMK_TARGET_SUCCESS ? CCTRUE : CCFALSE;
}

typedef struct cmk_sched_job {
    const CMKExecutorOps *ops;
    void *user;
    const CMKPlanTarget *target;
    CMKTargetId id;
    CMKExecutorResult result;
    CCBOOL ok;
} CMKSchedJob;

#ifdef CC_WINDOWS
typedef HANDLE CMKSchedThread;
static DWORD WINAPI cmk_sched_job_entry(LPVOID param)
{
    CMKSchedJob *job = (CMKSchedJob *)param;
    job->ok = job->ops->run(job->user, job->target, &job->result);
    return 0;
}
#else
typedef pthread_t CMKSchedThread;
static void *cmk_sched_job_entry(void *param)
{
    CMKSchedJob *job = (CMKSchedJob *)param;
    job->ok = job->ops->run(job->user, job->target, &job->result);
    return NULL;
}
#endif

void cmk_scheduler_set_jobs(CMKScheduler *scheduler, CCUINT32 jobs)
{
    if (scheduler)
        scheduler->jobs = jobs > 0 ? jobs : 1;
}

void cmk_scheduler_set_cache(
    CMKScheduler *scheduler, const CMKCacheOps *ops, void *user
)
{
    if (!scheduler)
        return;
    scheduler->cache = ops;
    scheduler->cache_user = user;
}

void cmk_scheduler_set_force(CMKScheduler *scheduler, CCBOOL force)
{
    if (scheduler)
        scheduler->force = force;
}

CCBOOL cmk_scheduler_run(CMKScheduler *scheduler, CCBOOL keep_going)
{
    if (!scheduler)
        return CCFALSE;
    if (scheduler->jobs <= 1)
        return cmk_scheduler_run_serial(scheduler, keep_going);

    cmk_sched_mark_reachable(scheduler);
    CMKTargetId root = cmk_plan_root(scheduler->plan);
    CMKSchedJob *batch = ccalloc(
        NULL, (CCUINT64)scheduler->jobs * sizeof(CMKSchedJob),
        CMK_SCHED_COMMENT
    );
    CMKSchedThread *threads = ccalloc(
        NULL, (CCUINT64)scheduler->jobs * sizeof(CMKSchedThread),
        CMK_SCHED_COMMENT
    );
    if (!batch || !threads)
    {
        if (batch)
            ccalloc(batch, 0, NULL);
        if (threads)
            ccalloc(threads, 0, NULL);
        return CCFALSE;
    }

    CCBOOL triggered = CCFALSE;
    CCBOOL ok = CCFALSE;
    for (;;)
    {
        /* 依赖失败传播 */
        for (CCUINT32 i = 0; i < scheduler->count; i++)
        {
            if (!cmk_sched_is_reachable(scheduler, i))
                continue;
            const CMKPlanTarget *target =
                cmk_plan_target_at(scheduler->plan, i);
            if (target->state == CMK_TARGET_PENDING &&
                cmk_sched_dep_terminal_bad(scheduler, i))
            {
                cmk_plan_set_target_state(
                    scheduler->plan, i, CMK_TARGET_BLOCKED
                );
                cmk_sched_set_message(
                    scheduler, i, "依赖失败或取消"
                );
            }
        }
        if (triggered)
        {
            for (CCUINT32 i = 0; i < scheduler->count; i++)
            {
                if (!cmk_sched_is_reachable(scheduler, i))
                    continue;
                CMKTargetState state =
                    cmk_plan_target_at(scheduler->plan, i)->state;
                if (state == CMK_TARGET_PENDING ||
                    state == CMK_TARGET_READY)
                    cmk_plan_set_target_state(
                        scheduler->plan, i, CMK_TARGET_CANCELLED
                    );
            }
            break;
        }

        CCUINT32 job_count = 0;
        CCBOOL aggregates = CCFALSE;
        for (CCUINT32 i = 0; i < scheduler->count &&
             job_count < scheduler->jobs; i++)
        {
            if (!cmk_sched_is_reachable(scheduler, i))
                continue;
            const CMKPlanTarget *target =
                cmk_plan_target_at(scheduler->plan, i);
            if (target->state != CMK_TARGET_PENDING ||
                !cmk_sched_deps_success(scheduler, i))
                continue;
            if (target->actions.size == 0)
            {
                cmk_plan_set_target_state(
                    scheduler->plan, i, CMK_TARGET_SUCCESS
                );
                aggregates = CCTRUE;
                continue;
            }
            if (scheduler->cache && !scheduler->force &&
                scheduler->cache->should_skip)
            {
                CCBOOL skip = CCFALSE;
                if (scheduler->cache->should_skip(
                        scheduler->cache_user, target, &skip
                    ) && skip)
                {
                    cmk_plan_set_target_state(
                        scheduler->plan, i, CMK_TARGET_SKIPPED
                    );
                    aggregates = CCTRUE;
                    continue;
                }
            }
            cmk_plan_set_target_state(
                scheduler->plan, i, CMK_TARGET_RUNNING
            );
            batch[job_count].ops = scheduler->ops;
            batch[job_count].user = scheduler->user;
            batch[job_count].target = target;
            batch[job_count].id = i;
            batch[job_count].ok = CCFALSE;
            memset(
                &batch[job_count].result, 0,
                sizeof(batch[job_count].result)
            );
            job_count++;
        }

        if (job_count == 0)
        {
            if (aggregates)
                continue;
            break;
        }

        CCUINT32 started = 0;
        for (CCUINT32 i = 0; i < job_count; i++)
        {
#ifdef CC_WINDOWS
            threads[i] = CreateThread(
                NULL, 0, cmk_sched_job_entry, &batch[i], 0, NULL
            );
            if (threads[i])
                started++;
            else
            {
                batch[i].ok = scheduler->ops->run(
                    scheduler->user, batch[i].target,
                    &batch[i].result
                );
                started++;
            }
#else
            if (pthread_create(
                    &threads[i], NULL, cmk_sched_job_entry, &batch[i]
                ) == 0)
                started++;
            else
            {
                batch[i].ok = scheduler->ops->run(
                    scheduler->user, batch[i].target,
                    &batch[i].result
                );
                started++;
            }
#endif
        }
        for (CCUINT32 i = 0; i < started; i++)
        {
#ifdef CC_WINDOWS
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
#else
            pthread_join(threads[i], NULL);
#endif
            CMKTargetId id = batch[i].id;
            scheduler->executed++;
            if (batch[i].ok && batch[i].result.success)
            {
                cmk_plan_set_target_state(
                    scheduler->plan, id, CMK_TARGET_SUCCESS
                );
                if (scheduler->cache && scheduler->cache->record)
                    (void)scheduler->cache->record(
                        scheduler->cache_user, batch[i].target
                    );
            }
            else
            {
                scheduler->failed++;
                cmk_plan_set_target_state(
                    scheduler->plan, id, CMK_TARGET_FAILED
                );
                cmk_sched_set_message(
                    scheduler, id,
                    batch[i].result.message
                        ? batch[i].result.message : "目标执行失败"
                );
                if (!keep_going)
                    triggered = CCTRUE;
            }
            if (batch[i].result.output.len > 0)
            {
                fwrite(
                    batch[i].result.output.data, 1,
                    batch[i].result.output.len, stdout
                );
            }
            cmk_executor_result_clear(&batch[i].result);
            fflush(stdout);
        }
    }

    ok = cmk_plan_target_at(scheduler->plan, root)->state ==
        CMK_TARGET_SUCCESS ? CCTRUE : CCFALSE;
    ccalloc(batch, 0, NULL);
    ccalloc(threads, 0, NULL);
    return ok;
}

CMKTargetState cmk_scheduler_target_state(
    const CMKScheduler *scheduler, CMKTargetId id
)
{
    if (!scheduler || id >= scheduler->count)
        return CMK_TARGET_PENDING;
    return cmk_plan_target_at(scheduler->plan, id)->state;
}

const char *cmk_scheduler_target_message(
    const CMKScheduler *scheduler, CMKTargetId id
)
{
    if (!scheduler || id >= scheduler->count ||
        !scheduler->messages[id])
        return NULL;
    return scheduler->messages[id];
}

CCUINT32 cmk_scheduler_executed_count(const CMKScheduler *scheduler)
{
    return scheduler ? scheduler->executed : 0;
}

CCUINT32 cmk_scheduler_failed_count(const CMKScheduler *scheduler)
{
    return scheduler ? scheduler->failed : 0;
}
