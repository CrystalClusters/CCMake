/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 单线程 Scheduler（M6）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_scheduler.h"
#include "cmk_ir_internal.h"
#include "cmk_target_internal.h"
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

    CMKTargetState root_state =
        cmk_plan_target_at(scheduler->plan, root)->state;
    return (root_state == CMK_TARGET_SUCCESS ||
            root_state == CMK_TARGET_SKIPPED)
        ? CCTRUE : CCFALSE;
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

static void cmk_sched_propagate_blocked(CMKScheduler *scheduler)
{
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
}

static void cmk_sched_cancel_pending(CMKScheduler *scheduler)
{
    for (CCUINT32 i = 0; i < scheduler->count; i++)
    {
        if (!cmk_sched_is_reachable(scheduler, i))
            continue;
        CMKTargetState state =
            cmk_plan_target_at(scheduler->plan, i)->state;
        if (state == CMK_TARGET_PENDING || state == CMK_TARGET_READY)
            cmk_plan_set_target_state(
                scheduler->plan, i, CMK_TARGET_CANCELLED
            );
    }
}

static void cmk_sched_collect_batch(
    CMKScheduler *scheduler, CMKSchedJob *batch,
    CCUINT32 *job_count, CCBOOL *aggregates
)
{
    *job_count = 0;
    *aggregates = CCFALSE;
    for (CCUINT32 i = 0; i < scheduler->count &&
         *job_count < scheduler->jobs; i++)
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
            *aggregates = CCTRUE;
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
                *aggregates = CCTRUE;
                continue;
            }
        }
        cmk_plan_set_target_state(
            scheduler->plan, i, CMK_TARGET_RUNNING
        );
        CMKSchedJob *job = &batch[*job_count];
        job->ops = scheduler->ops;
        job->user = scheduler->user;
        job->target = target;
        job->id = i;
        job->ok = CCFALSE;
        memset(&job->result, 0, sizeof(job->result));
        (*job_count)++;
    }
}

static void cmk_sched_start_batch(
    CMKScheduler *scheduler, CMKSchedJob *batch,
    CMKSchedThread *threads, CCUINT32 job_count
)
{
    for (CCUINT32 i = 0; i < job_count; i++)
    {
#ifdef CC_WINDOWS
        threads[i] = CreateThread(
            NULL, 0, cmk_sched_job_entry, &batch[i], 0, NULL
        );
        if (!threads[i])
            batch[i].ok = scheduler->ops->run(
                scheduler->user, batch[i].target,
                &batch[i].result
            );
#else
        if (pthread_create(
                &threads[i], NULL, cmk_sched_job_entry, &batch[i]
            ) != 0)
            batch[i].ok = scheduler->ops->run(
                scheduler->user, batch[i].target,
                &batch[i].result
            );
#endif
    }
}

static void cmk_sched_finish_batch(
    CMKScheduler *scheduler, CMKSchedJob *batch,
    CMKSchedThread *threads, CCUINT32 job_count,
    CCBOOL keep_going, CCBOOL *triggered
)
{
    for (CCUINT32 i = 0; i < job_count; i++)
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
                *triggered = CCTRUE;
        }
        if (batch[i].result.output.len > 0)
            fwrite(
                batch[i].result.output.data, 1,
                batch[i].result.output.len, stdout
            );
        cmk_executor_result_clear(&batch[i].result);
        fflush(stdout);
    }
}

static CCBOOL cmk_sched_root_ok(const CMKScheduler *scheduler)
{
    CMKTargetState state = cmk_plan_target_at(
        scheduler->plan, cmk_plan_root(scheduler->plan)
    )->state;
    return (state == CMK_TARGET_SUCCESS ||
            state == CMK_TARGET_SKIPPED)
        ? CCTRUE : CCFALSE;
}

CCBOOL cmk_scheduler_run(CMKScheduler *scheduler, CCBOOL keep_going)
{
    if (!scheduler)
        return CCFALSE;
    if (scheduler->jobs <= 1)
        return cmk_scheduler_run_serial(scheduler, keep_going);

    cmk_sched_mark_reachable(scheduler);
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
        cmk_sched_propagate_blocked(scheduler);
        if (triggered)
        {
            cmk_sched_cancel_pending(scheduler);
            break;
        }

        CCUINT32 job_count = 0;
        CCBOOL aggregates = CCFALSE;
        cmk_sched_collect_batch(
            scheduler, batch, &job_count, &aggregates
        );

        if (job_count == 0)
        {
            if (aggregates)
                continue;
            break;
        }

        cmk_sched_start_batch(
            scheduler, batch, threads, job_count
        );
        cmk_sched_finish_batch(
            scheduler, batch, threads, job_count,
            keep_going, &triggered
        );
    }

    ok = cmk_sched_root_ok(scheduler);
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
