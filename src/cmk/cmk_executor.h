/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK Scheduler 与 Executor 的边界接口（M6）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_EXECUTOR_H_
#define _INCLUDE_CMK_EXECUTOR_H_

#include "cmk_runtime.h"
#include "cmk_target.h"

/* Executor 返回的结构化结果；message 由 Executor 持有到下一次 run */
typedef struct cmk_executor_result {
    CCBOOL success;
    CCINT32 exit_code;
    const char *message;
    CMKString output;
} CMKExecutorResult;

/*
 * Scheduler 只通过本接口执行目标；不直接创建进程或操作文件。
 * user 由 Scheduler 原样传入。
 */
typedef CCBOOL (*CMKExecutorRun)(
    void *user, const CMKPlanTarget *target,
    CMKExecutorResult *result
);
typedef void (*CMKExecutorCancel)(
    void *user, CMKTargetId target
);

typedef struct cmk_executor_ops {
    CMKExecutorRun run;
    CMKExecutorCancel cancel;
} CMKExecutorOps;

typedef struct cmk_executor CMKExecutor;

CMKExecutor *cmk_executor_create(
    const CMKExecutionPolicy *policy,
    CMKDiagnostic *diagnostic
);
void cmk_executor_destroy(CMKExecutor *executor);
const CMKExecutorOps *cmk_executor_ops(void);
void cmk_executor_result_clear(CMKExecutorResult *result);

#endif
