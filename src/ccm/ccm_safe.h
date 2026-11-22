/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CCM 动态数组追加的安全检查
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCM_SAFE_H_
#define _INCLUDE_CCM_SAFE_H_

#include <cc_dyn.h>
#include <crystal_cluster.h>

/* 追加前检查元素尺寸与 CCDyn.size 的加法/表示边界 */
CCBOOL ccm_dyn_push(
    CCDyn *dyn, CCUINT32 align, CCINT32 size, const void *data
);

#endif
