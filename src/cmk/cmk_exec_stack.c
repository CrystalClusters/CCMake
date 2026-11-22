/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 运行时执行栈接口；语句 VM 已显式管理调用深度，这里直接执行
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_exec_stack.h"

CCBOOL cmk_exec_stack_run(
    CMKExecStackFn fn, void *user, CCBOOL *out
)
{
    if (!fn || !out)
        return CCFALSE;
    *out = fn(user);
    return CCTRUE;
}

CCBOOL cmk_exec_stack_check(size_t margin)
{
    (void)margin;
    return CCTRUE;
}
