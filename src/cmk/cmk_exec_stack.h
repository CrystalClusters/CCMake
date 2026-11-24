/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 运行时托管执行栈
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_EXEC_STACK_H_
#define _INCLUDE_CMK_EXEC_STACK_H_

#include <crystal_cluster.h>
#include <stddef.h>

typedef CCBOOL (*CMKExecStackFn)(void *user);

/*
 * 在独立的托管执行栈上运行 fn。
 * 返回 CCTRUE 表示执行完成，*out 为 fn 返回值；
 * 返回 CCFALSE 表示执行栈创建失败，*out 不可用。
 */
CCBOOL cmk_exec_stack_run(
    CMKExecStackFn fn, void *user, CCBOOL *out
);

/* 在托管执行栈内检查剩余空间；不在托管栈内时恒为 CCTRUE */
CCBOOL cmk_exec_stack_check(size_t margin);

#endif
