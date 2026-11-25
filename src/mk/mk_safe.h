/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 尺寸运算与动态数组追加的安全检查
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_SAFE_H_
#define _INCLUDE_MK_SAFE_H_

#include <cc_dyn.h>
#include <crystal_cluster.h>

/* 无符号加法溢出检查 */
CCBOOL mk_safe_add_u64(CCUINT64 a, CCUINT64 b, CCUINT64 *out);
/* 无符号乘法溢出检查 */
CCBOOL mk_safe_mul_u64(CCUINT64 a, CCUINT64 b, CCUINT64 *out);
/*
 * 追加前检查 CCDyn.size 与元素尺寸的加法/表示边界，再调用 cc_dyn_core_push。
 * 返回 CCTRUE 表示成功；失败不改变 CCDyn。
 */
CCBOOL mk_dyn_push(
    CCDyn *dyn, CCUINT32 align, CCINT32 size, const void *data
);

#endif
