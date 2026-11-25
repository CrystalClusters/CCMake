/*
 * @Description: MK 动作执行接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_EXECUTOR_H_
#define _INCLUDE_MK_EXECUTOR_H_

#include "mk_diagnostic.h"
#include "mk_policy.h"
#include "mk_value.h"

/* Executor 返回的结构化结果；message 由 Executor 持有到下一次执行 */
typedef struct mk_executor_result {
    CCBOOL success;
    CCINT32 exit_code;
    const char *message;
    MKString output;
} MKExecutorResult;

typedef struct mk_executor MKExecutor;

MKExecutor *mk_executor_create(
    const MKExecutionPolicy *policy,
    MKDiagnostic *diagnostic
);
void mk_executor_destroy(MKExecutor *executor);
CCBOOL mk_executor_run_actions(
    MKExecutor *executor,
    const MKAction *actions, CCUINT32 count,
    MKExecutorResult *result
);
void mk_executor_result_clear(MKExecutorResult *result);

#endif
