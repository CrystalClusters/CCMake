/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 尺寸运算与动态数组追加的安全检查
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_safe.h"

CCBOOL cmk_safe_add_u64(CCUINT64 a, CCUINT64 b, CCUINT64 *out)
{
    if (!out || a > (CCUINT64)-1 - b)
        return CCFALSE;
    *out = a + b;
    return CCTRUE;
}

CCBOOL cmk_safe_mul_u64(CCUINT64 a, CCUINT64 b, CCUINT64 *out)
{
    if (!out)
        return CCFALSE;
    if (a != 0 && b > (CCUINT64)-1 / a)
        return CCFALSE;
    *out = a * b;
    return CCTRUE;
}

CCBOOL cmk_dyn_push(
    CCDyn *dyn, CCUINT32 align, CCINT32 size, const void *data
)
{
    if (!dyn || !data || size <= 0 || align == 0)
        return CCFALSE;
    if ((CCUINT32)size > (CCUINT32)-1 - dyn->size)
        return CCFALSE;
    return (cc_dyn_core_push(dyn, align, size, (void *)data) == 0)
        ? CCTRUE : CCFALSE;
}
