/*
 * @Description: CMK 执行策略（独立于 Runtime 的公共策略模型）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_POLICY_H_
#define _INCLUDE_CMK_POLICY_H_

#include "cmk_value.h"

/* 可配置执行策略；0 表示不限制 */
typedef struct cmk_execution_policy {
    CCUINT32 max_recursion_depth;
    CCUINT64 max_steps;
    CCUINT64 max_configure_time_ms;
    CCUINT32 max_find_depth;
    CCUINT32 max_processes;
    CCUINT64 max_output_capture;
} CMKExecutionPolicy;

void cmk_execution_policy_init(CMKExecutionPolicy *policy);

#endif
