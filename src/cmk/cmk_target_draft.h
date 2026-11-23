/*
 * @Description: CMK Configure 输出给 Lowering 的目标草案
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_TARGET_DRAFT_H_
#define _INCLUDE_CMK_TARGET_DRAFT_H_

#include "cmk_semantic.h"
#include "cmk_value.h"

typedef struct cmk_target_draft {
    const CMKTargetInfo *info;
    CMKValue depends_on;   /* list<target_ref> */
    CMKValue inputs;       /* list<string> */
    CMKValue outputs;      /* list<string> */
    CMKValue actions;      /* list<action> */
} CMKTargetDraft;

#endif
