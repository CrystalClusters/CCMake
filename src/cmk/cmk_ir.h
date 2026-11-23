/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 冻结计划、Lowering 与验证（M5）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_IR_H_
#define _INCLUDE_CMK_IR_H_

#include "cmk_diagnostic.h"
#include "cmk_target.h"
#include "cmk_target_draft.h"

typedef struct cmk_plan CMKPlan;

CMKPlan *cmk_plan_create(void);
void cmk_plan_destroy(CMKPlan *plan);

/*
 * 把一个模块的 target draft 复制进 Plan Builder。
 * draft 只借用；字符串、动作、依赖全部复制。
 */
CCBOOL cmk_plan_add_draft(
    CMKPlan *plan,
    const char *module_path, CCUINT32 module_len,
    const CMKTargetDraft *draft,
    CMKDiagnostic *diagnostic
);

/* 解析依赖、检查重复/环/输出冲突/动作参数，并生成拓扑序 */
CCBOOL cmk_plan_freeze(
    CMKPlan *plan,
    const char *root_module, CCUINT32 root_module_len,
    const char *root_name, CCUINT32 root_name_len,
    CMKDiagnostic *diagnostic
);

CCBOOL cmk_plan_is_frozen(const CMKPlan *plan);
CCUINT32 cmk_plan_target_count(const CMKPlan *plan);
const CMKPlanTarget *cmk_plan_target_at(
    const CMKPlan *plan, CMKTargetId id
);
CMKTargetId cmk_plan_find_target(
    const CMKPlan *plan,
    const char *module_path, CCUINT32 module_len,
    const char *name, CCUINT32 name_len
);
CMKTargetId cmk_plan_root(const CMKPlan *plan);
CCUINT32 cmk_plan_order_count(const CMKPlan *plan);
CMKTargetId cmk_plan_order_at(const CMKPlan *plan, CCUINT32 index);

#endif
