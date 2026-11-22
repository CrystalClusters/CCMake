/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 单线程 Scheduler（M6）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_SCHEDULER_H_
#define _INCLUDE_CMK_SCHEDULER_H_

#include "cmk_executor.h"
#include "cmk_ir.h"

typedef struct cmk_scheduler CMKScheduler;

/* Scheduler 只依赖抽象缓存接口，不直接依赖 cmk_cache */
typedef CCBOOL (*CMKSchedCacheShouldSkip)(
    void *user, const CMKPlanTarget *target, CCBOOL *skip
);
typedef CCBOOL (*CMKSchedCacheRecord)(
    void *user, const CMKPlanTarget *target
);
typedef struct cmk_cache_ops {
    CMKSchedCacheShouldSkip should_skip;
    CMKSchedCacheRecord record;
} CMKCacheOps;

CMKScheduler *cmk_scheduler_create(
    CMKPlan *plan,
    const CMKExecutorOps *ops, void *user,
    CMKDiagnostic *diagnostic
);
void cmk_scheduler_destroy(CMKScheduler *scheduler);

/*
 * 构建 root 依赖闭包并执行。
 * keep_going 为 CCFALSE 时 fail-fast；为 CCTRUE 时继续独立任务。
 * 返回 CCTRUE 表示根目标 SUCCESS，否则 CCFALSE。
 */
CCBOOL cmk_scheduler_run(
    CMKScheduler *scheduler, CCBOOL keep_going
);
/* M9：READY 批次并发上限；0/1 表示串行 */
void cmk_scheduler_set_jobs(
    CMKScheduler *scheduler, CCUINT32 jobs
);
/* 可选缓存；force 对应 -B */
void cmk_scheduler_set_cache(
    CMKScheduler *scheduler, const CMKCacheOps *ops, void *user
);
void cmk_scheduler_set_force(
    CMKScheduler *scheduler, CCBOOL force
);
CMKTargetState cmk_scheduler_target_state(
    const CMKScheduler *scheduler, CMKTargetId id
);
/* 失败/阻塞目标的消息；无消息返回 NULL */
const char *cmk_scheduler_target_message(
    const CMKScheduler *scheduler, CMKTargetId id
);
CCUINT32 cmk_scheduler_executed_count(const CMKScheduler *scheduler);
CCUINT32 cmk_scheduler_failed_count(const CMKScheduler *scheduler);

#endif
