/*
 * @Description: CMK IR 受控内部写接口；仅 Scheduler 可写目标运行状态
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_IR_INTERNAL_H_
#define _INCLUDE_CMK_IR_INTERNAL_H_

#include "cmk_ir.h"

/* 只有 Scheduler 能调用；Plan 冻结后状态更新的唯一入口。 */
void cmk_plan_set_target_state(
    CMKPlan *plan, CMKTargetId id, CMKTargetState state
);

#endif
