/*
 * @Description: CMK 动作执行接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_EXECUTOR_H_
#define _INCLUDE_CMK_EXECUTOR_H_

#include "cmk_diagnostic.h"
#include "cmk_policy.h"
#include "cmk_value.h"

/* Executor 返回的结构化结果；message 由 Executor 持有到下一次执行 */
typedef struct cmk_executor_result {
    CCBOOL success;
    CCINT32 exit_code;
    const char *message;
    CMKString output;
} CMKExecutorResult;

typedef struct cmk_executor CMKExecutor;

CMKExecutor *cmk_executor_create(
    const CMKExecutionPolicy *policy,
    CMKDiagnostic *diagnostic
);
void cmk_executor_destroy(CMKExecutor *executor);
CCBOOL cmk_executor_run_actions(
    CMKExecutor *executor,
    const CMKAction *actions, CCUINT32 count,
    CMKExecutorResult *result
);
void cmk_executor_result_clear(CMKExecutorResult *result);

#endif
