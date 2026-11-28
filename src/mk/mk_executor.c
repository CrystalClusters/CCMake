/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 动作执行
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_executor.h"
#include "mk_process.h"
#include "mk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>
#include <cc_encoding.h>
#include <cchost.h>

#include <string.h>

#define MK_EXECUTOR_COMMENT CC_DBG_REL_TAG(".mk 执行", NULL)

struct mk_executor {
    MKExecutionPolicy policy;
    MKDiagnostic *diagnostic;
};

static CCUINT32 mk_executor_list_count(const MKList *list)
{
    return list ? mk_list_count(list) : 0;
}

static char **mk_executor_list_strings(const MKList *list)
{
    CCUINT32 count = mk_executor_list_count(list);
    char **items = ccalloc(
        NULL, (CCUINT64)(count + 1) * sizeof(char *),
        MK_EXECUTOR_COMMENT
    );
    if (!items)
        return NULL;
    memset(items, 0, (size_t)(count + 1) * sizeof(char *));
    for (CCUINT32 i = 0; i < count; i++)
    {
        const MKValue *value = mk_list_at(list, i);
        if (!value || value->type != MK_VALUE_STRING)
        {
            ccalloc(items, 0, NULL);
            return NULL;
        }
        items[i] = value->as.string.data;
    }
    return items;
}

static char *mk_executor_local_path(const char *path)
{
    return cc_utf8_to_local(path, CC_LOCAL_SYSTEM);
}

static CCBOOL mk_executor_file_action(const MKAction *action)
{
    CCBOOL ok = CCFALSE;
    char *from = NULL;
    char *to = NULL;
    char *path = NULL;
    switch (action->kind)
    {
    case MK_ACTION_COPY:
    case MK_ACTION_MOVE:
        from = mk_executor_local_path(action->from.data);
        to = mk_executor_local_path(action->to.data);
        if (from && to)
            ok = (action->kind == MK_ACTION_MOVE
                ? cchost_move(from, to)
                : cchost_copy(from, to)) == CC_HOST_OK
                ? CCTRUE : CCFALSE;
        break;

    case MK_ACTION_REMOVE:
        path = mk_executor_local_path(action->path.data);
        if (path)
            ok = cchost_remove(path) == CC_HOST_OK
                ? CCTRUE : CCFALSE;
        break;

    case MK_ACTION_MKDIR:
        path = mk_executor_local_path(action->path.data);
        if (path)
            ok = cchost_mkdirs(path) == CC_HOST_OK
                ? CCTRUE : CCFALSE;
        break;

    default:
        break;
    }
    if (from)
        ccalloc(from, 0, NULL);
    if (to)
        ccalloc(to, 0, NULL);
    if (path)
        ccalloc(path, 0, NULL);
    return ok;
}

static CCBOOL mk_executor_exec(
    MKExecutor *executor, const MKAction *action,
    MKExecutorResult *result
)
{
    CCUINT32 argc = mk_executor_list_count(action->args);
    CCUINT32 envc = mk_executor_list_count(action->env);
    char **argv = mk_executor_list_strings(action->args);
    char **envp = mk_executor_list_strings(action->env);
    if ((argc > 0 && !argv) || (envc > 0 && !envp))
    {
        if (argv)
            ccalloc(argv, 0, NULL);
        if (envp)
            ccalloc(envp, 0, NULL);
        result->success = CCFALSE;
        result->message = "参数内存分配失败";
        return CCFALSE;
    }

    MKProcessSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.program = action->program.data;
    spec.argv = (const char *const *)argv;
    spec.envp = envc > 0 ? (const char *const *)envp : NULL;
    spec.working_dir = action->cwd.len ? action->cwd.data : NULL;
    spec.capture_limit = executor->policy.max_output_capture;
    MKProcess *process = mk_process_create(&spec);
    CCBOOL ok = process && mk_process_start(process);
    MKProcessResult process_result;
    memset(&process_result, 0, sizeof(process_result));
    if (ok)
        ok = mk_process_wait(process, &process_result);
    if (!ok)
    {
        result->success = CCFALSE;
        result->message = "进程启动或等待失败";
    }
    else
    {
        CCBOOL output_failed = CCFALSE;
        if (process_result.output.len > 0)
        {
            if (!mk_string_append(
                    &result->output, process_result.output.data,
                    process_result.output.len
                ))
                output_failed = CCTRUE;
        }
        if (output_failed)
        {
            result->success = CCFALSE;
            result->message = "输出缓存失败";
        }
        else if (process_result.capture_failed)
        {
            result->success = CCFALSE;
            result->message = "输出捕获超过限制";
        }
        else if (process_result.signal != 0)
        {
            result->success = CCFALSE;
            result->message = "进程被信号终止";
        }
        else if (process_result.exit_code != 0)
        {
            result->success = CCFALSE;
            result->message = "进程退出码非零";
        }
        else
            result->success = CCTRUE;
        result->exit_code = process_result.exit_code;
    }
    mk_process_result_clear(&process_result);
    if (process)
        mk_process_destroy(process);
    if (argv)
        ccalloc(argv, 0, NULL);
    if (envp)
        ccalloc(envp, 0, NULL);
    return result->success;
}

CCBOOL mk_executor_run_actions(
    MKExecutor *executor, const MKAction *actions,
    CCUINT32 count, MKExecutorResult *result
)
{
    if (!executor || (!actions && count > 0) || !result)
        return CCFALSE;
    memset(result, 0, sizeof(*result));
    result->success = CCTRUE;
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (actions[i].kind == MK_ACTION_EXEC)
        {
            if (!mk_executor_exec(executor, &actions[i], result))
                return CCFALSE;
        }
        else if (!mk_executor_file_action(&actions[i]))
        {
            result->success = CCFALSE;
            result->message = "文件动作失败";
            return CCFALSE;
        }
    }
    return CCTRUE;
}

MKExecutor *mk_executor_create(
    const MKExecutionPolicy *policy,
    MKDiagnostic *diagnostic
)
{
    MKExecutor *executor = ccalloc(
        NULL, sizeof(MKExecutor), MK_EXECUTOR_COMMENT
    );
    if (!executor)
        return NULL;
    if (policy)
        executor->policy = *policy;
    else
        mk_execution_policy_init(&executor->policy);
    executor->diagnostic = diagnostic;
    return executor;
}

void mk_executor_destroy(MKExecutor *executor)
{
    if (executor)
        ccalloc(executor, 0, NULL);
}

void mk_executor_result_clear(MKExecutorResult *result)
{
    if (!result)
        return;
    mk_string_clear(&result->output);
    memset(result, 0, sizeof(*result));
}
