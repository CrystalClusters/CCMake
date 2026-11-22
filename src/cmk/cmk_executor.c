/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 真实 Executor 与文件动作（M7）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_executor.h"
#include "cmk_process.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdio.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define CMK_EXECUTOR_COMMENT CC_DBG_REL_TAG(".cmk 执行", NULL)

struct cmk_executor {
    CMKExecutionPolicy policy;
    CMKDiagnostic *diagnostic;
};

static char **cmk_executor_string_array(
    const CCDyn *array, CCUINT32 count
)
{
    char **items = ccalloc(
        NULL, (CCUINT64)(count + 1) * sizeof(char *),
        CMK_EXECUTOR_COMMENT
    );
    if (!items)
        return NULL;
    memset(items, 0, (size_t)(count + 1) * sizeof(char *));
    const CMKString *strings = (const CMKString *)array->array;
    for (CCUINT32 i = 0; i < count; i++)
        items[i] = strings[i].data;
    return items;
}

static CCBOOL cmk_executor_copy_file(
    const char *from, const char *to
)
{
    FILE *input = fopen(from, "rb");
    if (!input)
        return CCFALSE;
    FILE *output = fopen(to, "wb");
    if (!output)
    {
        fclose(input);
        return CCFALSE;
    }
    char buffer[64 * 1024];
    size_t read = 0;
    CCBOOL ok = CCTRUE;
    while ((read = fread(buffer, 1, sizeof(buffer), input)) > 0)
    {
        if (fwrite(buffer, 1, read, output) != read)
        {
            ok = CCFALSE;
            break;
        }
    }
    if (ferror(input))
        ok = CCFALSE;
    fclose(input);
    if (fclose(output) != 0)
        ok = CCFALSE;
    if (!ok)
        remove(to);
    return ok;
}

static CCBOOL cmk_executor_make_dir_one(const char *path)
{
#ifdef CC_WINDOWS
    if (CreateDirectoryA(path, NULL))
        return CCTRUE;
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? CCTRUE : CCFALSE;
#else
    if (mkdir(path, 0777) == 0)
        return CCTRUE;
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        ? CCTRUE : CCFALSE;
#endif
}

static CCBOOL cmk_executor_mkdir(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;
    char *copy = ccalloc(
        NULL, (CCUINT64)strlen(path) + 1, CMK_EXECUTOR_COMMENT
    );
    if (!copy)
        return CCFALSE;
    strcpy(copy, path);
    CCBOOL ok = CCTRUE;
    for (char *p = copy + 1; *p; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            char saved = *p;
            *p = '\0';
            if (!cmk_executor_make_dir_one(copy))
            {
                ok = CCFALSE;
                *p = saved;
                break;
            }
            *p = saved;
        }
    }
    if (ok)
        ok = cmk_executor_make_dir_one(copy);
    ccalloc(copy, 0, NULL);
    return ok;
}

static CCBOOL cmk_executor_remove(const char *path)
{
    if (remove(path) == 0)
        return CCTRUE;
#ifdef CC_WINDOWS
    return RemoveDirectoryA(path) ? CCTRUE : CCFALSE;
#else
    return rmdir(path) == 0 ? CCTRUE : CCFALSE;
#endif
}

static CCBOOL cmk_executor_file_action(const CMKPlanAction *action)
{
    switch (action->type)
    {
    case CMK_PLAN_ACTION_COPY:
        return cmk_executor_copy_file(
            action->from.data, action->to.data
        );
    case CMK_PLAN_ACTION_MOVE:
        if (rename(action->from.data, action->to.data) == 0)
            return CCTRUE;
        if (!cmk_executor_copy_file(
                action->from.data, action->to.data
            ))
            return CCFALSE;
        return cmk_executor_remove(action->from.data);
    case CMK_PLAN_ACTION_REMOVE:
        return cmk_executor_remove(action->path.data);
    case CMK_PLAN_ACTION_MKDIR:
        return cmk_executor_mkdir(action->path.data);
    default:
        return CCFALSE;
    }
}

static CCBOOL cmk_executor_exec(
    CMKExecutor *executor, const CMKPlanAction *action,
    CMKExecutorResult *result
)
{
    char **argv = cmk_executor_string_array(
        &action->argv, action->argc
    );
    char **envp = cmk_executor_string_array(
        &action->env, action->envc
    );
    if ((action->argc > 0 && !argv) ||
        (action->envc > 0 && !envp))
    {
        if (argv)
            ccalloc(argv, 0, NULL);
        if (envp)
            ccalloc(envp, 0, NULL);
        result->success = CCFALSE;
        result->message = "参数内存分配失败";
        return CCFALSE;
    }

    CMKProcessSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.program = action->program.data;
    spec.argv = (const char *const *)argv;
    spec.envp = (const char *const *)envp;
    spec.working_dir = action->cwd.len ? action->cwd.data : NULL;
    spec.capture_limit = executor->policy.max_output_capture;
    CMKProcess *process = cmk_process_create(&spec);
    CCBOOL ok = process && cmk_process_start(process);
    CMKProcessResult process_result;
    memset(&process_result, 0, sizeof(process_result));
    if (ok)
        ok = cmk_process_wait(process, &process_result);
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
            if (!cmk_string_append(
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
    cmk_process_result_clear(&process_result);
    if (process)
        cmk_process_destroy(process);
    if (argv)
        ccalloc(argv, 0, NULL);
    if (envp)
        ccalloc(envp, 0, NULL);
    return result->success;
}

static CCBOOL cmk_executor_run(
    void *user, const CMKPlanTarget *target,
    CMKExecutorResult *result
)
{
    CMKExecutor *executor = (CMKExecutor *)user;
    if (!executor || !target || !result)
        return CCFALSE;
    memset(result, 0, sizeof(*result));
    result->success = CCTRUE;

    const CMKPlanAction *actions =
        (const CMKPlanAction *)target->actions.array;
    CCUINT32 count = target->actions.size /
        (CCUINT32)sizeof(CMKPlanAction);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (actions[i].type == CMK_PLAN_ACTION_EXEC)
        {
            if (!cmk_executor_exec(executor, &actions[i], result))
                return CCFALSE;
        }
        else if (!cmk_executor_file_action(&actions[i]))
        {
            result->success = CCFALSE;
            result->message = "文件动作失败";
            return CCFALSE;
        }
    }
    return CCTRUE;
}

static void cmk_executor_cancel(void *user, CMKTargetId target)
{
    (void)user;
    (void)target;
}

CMKExecutor *cmk_executor_create(
    const CMKExecutionPolicy *policy,
    CMKDiagnostic *diagnostic
)
{
    CMKExecutor *executor = ccalloc(
        NULL, sizeof(CMKExecutor), CMK_EXECUTOR_COMMENT
    );
    if (!executor)
        return NULL;
    if (policy)
        executor->policy = *policy;
    else
        cmk_execution_policy_init(&executor->policy);
    executor->diagnostic = diagnostic;
    return executor;
}

void cmk_executor_destroy(CMKExecutor *executor)
{
    if (executor)
        ccalloc(executor, 0, NULL);
}

const CMKExecutorOps *cmk_executor_ops(void)
{
    static const CMKExecutorOps ops = {
        cmk_executor_run, cmk_executor_cancel
    };
    return &ops;
}

void cmk_executor_result_clear(CMKExecutorResult *result)
{
    if (!result)
        return;
    cmk_string_clear(&result->output);
    memset(result, 0, sizeof(*result));
}
