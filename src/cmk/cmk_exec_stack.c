/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 运行时托管执行栈
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif

#include "cmk_exec_stack.h"

#define CMK_EXEC_STACK_BYTES ((size_t)256 * 1024 * 1024)

#if defined(CC_WINDOWS)
#define CMK_EXEC_STACK_TLS
#else
#define CMK_EXEC_STACK_TLS
#endif

#if defined(CC_WINDOWS)

#include <windows.h>

typedef struct cmk_exec_stack_ctx {
    CMKExecStackFn fn;
    void *user;
    void *parent;
    char *base;
    CCBOOL result;
} CMKExecStackCtx;

static CMK_EXEC_STACK_TLS CMKExecStackCtx *g_cmk_exec_ctx = NULL;

static VOID CALLBACK cmk_exec_stack_entry(void *param)
{
    CMKExecStackCtx *ctx = (CMKExecStackCtx *)param;
    char marker = 0;
    ctx->base = &marker;
    g_cmk_exec_ctx = ctx;
    ctx->result = ctx->fn(ctx->user);
    g_cmk_exec_ctx = NULL;
    SwitchToFiber(ctx->parent);
}

CCBOOL cmk_exec_stack_run(
    CMKExecStackFn fn, void *user, CCBOOL *out
)
{
    if (!fn || !out)
        return CCFALSE;
    CMKExecStackCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fn = fn;
    ctx.user = user;

    void *parent = ConvertThreadToFiber(NULL);
    CCBOOL converted = (parent != NULL) ? CCTRUE : CCFALSE;
    if (!parent)
    {
        if (GetLastError() != ERROR_ALREADY_FIBER)
            return CCFALSE;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
        parent = GetCurrentFiber();
#pragma GCC diagnostic pop
    }
    ctx.parent = parent;

    void *fiber = CreateFiberEx(
        64 * 1024, CMK_EXEC_STACK_BYTES, 0,
        cmk_exec_stack_entry, &ctx
    );
    if (!fiber)
    {
        if (converted)
            ConvertFiberToThread();
        return CCFALSE;
    }

    SwitchToFiber(fiber);
    DeleteFiber(fiber);
    if (converted)
        ConvertFiberToThread();
    *out = ctx.result;
    return CCTRUE;
}

#elif defined(__unix__) || defined(__APPLE__)

#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

typedef struct cmk_exec_stack_ctx {
    CMKExecStackFn fn;
    void *user;
    char *base;
    CCBOOL result;
} CMKExecStackCtx;

static CMK_EXEC_STACK_TLS CMKExecStackCtx *g_cmk_exec_ctx = NULL;

static void cmk_exec_stack_entry(void *param)
{
    CMKExecStackCtx *ctx = (CMKExecStackCtx *)param;
    char marker = 0;
    ctx->base = &marker;
    g_cmk_exec_ctx = ctx;
    ctx->result = ctx->fn(ctx->user);
    g_cmk_exec_ctx = NULL;
}

CCBOOL cmk_exec_stack_run(
    CMKExecStackFn fn, void *user, CCBOOL *out
)
{
    if (!fn || !out)
        return CCFALSE;
    void *stack = malloc(CMK_EXEC_STACK_BYTES);
    if (!stack)
        return CCFALSE;

    CMKExecStackCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fn = fn;
    ctx.user = user;

    ucontext_t child;
    ucontext_t parent;
    if (getcontext(&child) != 0)
    {
        free(stack);
        return CCFALSE;
    }
    child.uc_stack.ss_sp = stack;
    child.uc_stack.ss_size = CMK_EXEC_STACK_BYTES;
    child.uc_link = &parent;
    makecontext(
        &child, (void (*)(void))cmk_exec_stack_entry, 1,
        (CCUINTPTR)&ctx
    );
    swapcontext(&parent, &child);
    free(stack);
    *out = ctx.result;
    return CCTRUE;
}

#else

CCBOOL cmk_exec_stack_run(
    CMKExecStackFn fn, void *user, CCBOOL *out
)
{
    if (!fn || !out)
        return CCFALSE;
    *out = fn(user);
    return CCTRUE;
}

#endif

#if defined(CC_WINDOWS) || defined(__unix__) || defined(__APPLE__)

CCBOOL cmk_exec_stack_check(size_t margin)
{
    CMKExecStackCtx *ctx = g_cmk_exec_ctx;
    if (!ctx || !ctx->base)
        return CCTRUE;
    char marker = 0;
    CCUINTPTR base = (CCUINTPTR)ctx->base;
    CCUINTPTR current = (CCUINTPTR)&marker;
    if (current >= base)
        return CCTRUE;
    size_t used = (size_t)(base - current);
    if (used >= CMK_EXEC_STACK_BYTES)
        return CCFALSE;
    return (CMK_EXEC_STACK_BYTES - used > margin)
        ? CCTRUE : CCFALSE;
}

#else

CCBOOL cmk_exec_stack_check(size_t margin)
{
    (void)margin;
    return CCTRUE;
}

#endif
