/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MC 动态数组追加的安全检查
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mc_safe.h"

CCBOOL mc_dyn_push(
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
