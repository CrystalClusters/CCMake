/*
 * @Description: CMK 构建计划 IR 内部布局；仅 IR/Scheduler/Executor/Cache 使用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_TARGET_INTERNAL_H_
#define _INCLUDE_CMK_TARGET_INTERNAL_H_

#include "cmk_target.h"

typedef struct cmk_target_key {
    CMKString module;
    CMKString name;
} CMKTargetKey;

typedef struct cmk_plan_action {
    CMKPlanActionType type;
    CMKString program;
    CCDyn argv;       /* CMKString[] */
    CCUINT32 argc;
    CCDyn env;        /* CMKString[] */
    CCUINT32 envc;
    CMKString cwd;
    CMKString from;
    CMKString to;
    CMKString path;
} CMKPlanAction;

typedef struct cmk_plan_target {
    CMKTargetKey key;
    CCDyn dependencies; /* CMKTargetId[] */
    CCDyn dependency_keys; /* CMKTargetKey[]：冻结前解析 */
    CCDyn actions;      /* CMKPlanAction[] */
    CCDyn inputs;       /* CMKString[] */
    CCDyn outputs;      /* CMKString[] */
    CMKTargetState state;
} CMKPlanTarget;

#endif
