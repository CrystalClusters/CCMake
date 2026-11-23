/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 冻结构建计划中的目标与动作 IR（M5）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_TARGET_H_
#define _INCLUDE_CMK_TARGET_H_

#include "cmk_value.h"

typedef CCUINT32 CMKTargetId;
#define CMK_TARGET_ID_NIL ((CMKTargetId)0xFFFFFFFFu)

typedef enum cmk_target_state {
    CMK_TARGET_PENDING = 0,
    CMK_TARGET_READY,
    CMK_TARGET_RUNNING,
    CMK_TARGET_SUCCESS,
    CMK_TARGET_FAILED,
    CMK_TARGET_BLOCKED,
    CMK_TARGET_SKIPPED,
    CMK_TARGET_CANCELLED
} CMKTargetState;

typedef enum cmk_plan_action_type {
    CMK_PLAN_ACTION_EXEC = 0,
    CMK_PLAN_ACTION_COPY,
    CMK_PLAN_ACTION_MOVE,
    CMK_PLAN_ACTION_REMOVE,
    CMK_PLAN_ACTION_MKDIR
} CMKPlanActionType;

typedef struct cmk_target_key CMKTargetKey;
typedef struct cmk_plan_action CMKPlanAction;
typedef struct cmk_plan_target CMKPlanTarget;

/*
 * 公开侧只暴露不透明句柄；构造与布局见 cmk_target_internal.h。
 * Scheduler/Executor/Cache/IR 通过内部头访问数组，其他模块只使用指针。
 */

void cmk_target_key_init(CMKTargetKey *key);
void cmk_target_key_clear(CMKTargetKey *key);
CCBOOL cmk_target_key_set(
    CMKTargetKey *key,
    const char *module, CCUINT32 module_len,
    const char *name, CCUINT32 name_len
);
CCBOOL cmk_target_key_equal(
    const CMKTargetKey *a, const CMKTargetKey *b
);

void cmk_plan_action_init(CMKPlanAction *action);
void cmk_plan_action_clear(CMKPlanAction *action);
CCBOOL cmk_plan_action_copy(
    CMKPlanAction *dst, const CMKAction *src
);
CCBOOL cmk_plan_action_append_arg(
    CMKPlanAction *action, const char *data, CCUINT32 len
);
CCBOOL cmk_plan_action_append_env(
    CMKPlanAction *action, const char *data, CCUINT32 len
);

void cmk_plan_target_init(CMKPlanTarget *target);
void cmk_plan_target_clear(CMKPlanTarget *target);
CCBOOL cmk_plan_target_add_dependency(
    CMKPlanTarget *target, CMKTargetId dependency
);
CCBOOL cmk_plan_target_add_dependency_key(
    CMKPlanTarget *target,
    const char *module, CCUINT32 module_len,
    const char *name, CCUINT32 name_len
);
CCBOOL cmk_plan_target_add_action(
    CMKPlanTarget *target, const CMKPlanAction *action
);
CCBOOL cmk_plan_target_add_input(
    CMKPlanTarget *target, const char *path, CCUINT32 len
);
CCBOOL cmk_plan_target_add_output(
    CMKPlanTarget *target, const char *path, CCUINT32 len
);

const char *cmk_target_state_name(CMKTargetState state);
const char *cmk_plan_action_type_name(CMKPlanActionType type);

#endif
