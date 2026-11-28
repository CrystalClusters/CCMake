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
#include <ccfiles.h>

#include <string.h>
#include <sys/stat.h>

#ifdef CC_WINDOWS
#include <direct.h>
#define mk_executor_getcwd _getcwd
#else
#include <unistd.h>
#define mk_executor_getcwd getcwd
#endif

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

static char *mk_executor_path_copy(const char *path)
{
    size_t len = strlen(path);
    char *copy = ccalloc(
        NULL, (CCUINT64)len + 1, MK_EXECUTOR_COMMENT
    );
    if (!copy)
        return NULL;
    for (size_t i = 0; i < len; i++)
        copy[i] = path[i] == '\\' ? '/' : path[i];
    copy[len] = '\0';
    return copy;
}

static char *mk_executor_absolute(const char *path)
{
    char *copy = mk_executor_path_copy(path);
    if (!copy)
        return NULL;
    int absolute = copy[0] == '/';
#ifdef CC_WINDOWS
    if (!absolute && copy[0] && copy[1] == ':')
        absolute = 1;
#endif
    if (absolute)
        return copy;

    char cwd[4096];
    if (!mk_executor_getcwd(cwd, (int)sizeof(cwd)))
    {
        ccalloc(copy, 0, NULL);
        return NULL;
    }
    size_t cwd_len = strlen(cwd);
    size_t len = strlen(copy);
    char *joined = ccalloc(
        NULL, (CCUINT64)(cwd_len + len + 2), MK_EXECUTOR_COMMENT
    );
    if (!joined)
    {
        ccalloc(copy, 0, NULL);
        return NULL;
    }
    for (size_t i = 0; i < cwd_len; i++)
        joined[i] = cwd[i] == '\\' ? '/' : cwd[i];
    joined[cwd_len] = '/';
    memcpy(joined + cwd_len + 1, copy, len + 1);
    ccalloc(copy, 0, NULL);
    return joined;
}

static char *mk_executor_parent(const char *path)
{
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        len--;
    size_t cut = len;
    while (cut > 0 && path[cut - 1] != '/')
        cut--;
    size_t parent_len = cut <= 1 ? 1 : cut - 1;
#ifdef CC_WINDOWS
    if (cut == 3 && path[1] == ':')
        parent_len = 3;
#endif
    char *parent = ccalloc(
        NULL, (CCUINT64)parent_len + 1, MK_EXECUTOR_COMMENT
    );
    if (!parent)
        return NULL;
    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';
    return parent;
}

static const char *mk_executor_base(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static char *mk_executor_vpath(const char *mount, const char *name)
{
    size_t mount_len = strlen(mount);
    size_t name_len = strlen(name);
    char *vpath = ccalloc(
        NULL, (CCUINT64)(mount_len + name_len + 2),
        MK_EXECUTOR_COMMENT
    );
    if (!vpath)
        return NULL;
    memcpy(vpath, mount, mount_len);
    vpath[mount_len] = '/';
    memcpy(vpath + mount_len + 1, name, name_len + 1);
    return vpath;
}

static CCBOOL mk_executor_create_dir(
    CCFiles *fs, const char *mount, const char *name
)
{
    char *vpath = mk_executor_vpath(mount, name);
    if (!vpath)
        return CCFALSE;
    CCINT32 result = ccfiles_create_dir(fs, vpath);
    if (result == CC_FS_ERR_EXIST)
    {
        CCFsStat stat;
        result = ccfiles_stat(fs, vpath, &stat);
        if (result == CC_FS_OK && stat.type == CC_FS_TYPE_DIR)
            result = CC_FS_OK;
        else
            result = CC_FS_ERR_EXIST;
    }
    ccalloc(vpath, 0, NULL);
    return result == CC_FS_OK ? CCTRUE : CCFALSE;
}

static CCBOOL mk_executor_mkdir(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;
    char *absolute = mk_executor_absolute(path);
    char *parent = absolute ? mk_executor_parent(absolute) : NULL;
    if (!absolute || !parent)
    {
        if (absolute)
            ccalloc(absolute, 0, NULL);
        if (parent)
            ccalloc(parent, 0, NULL);
        return CCFALSE;
    }
    if (strcmp(parent, absolute) != 0)
    {
        struct stat st;
        if (stat(parent, &st) != 0 &&
            !mk_executor_mkdir(parent))
        {
            ccalloc(parent, 0, NULL);
            ccalloc(absolute, 0, NULL);
            return CCFALSE;
        }
    }

    CCFiles *fs = ccfiles_create();
    CCBOOL ok = CCFALSE;
    if (fs && ccfiles_mount(
            fs, "/m", parent, CC_FS_MOUNT_DIR
        ) == CC_FS_OK)
        ok = mk_executor_create_dir(
            fs, "/m", mk_executor_base(absolute)
        );
    if (fs)
        ccfiles_destroy(fs);
    ccalloc(parent, 0, NULL);
    ccalloc(absolute, 0, NULL);
    return ok;
}

static CCBOOL mk_executor_transfer(
    const char *from, const char *to, CCBOOL move
)
{
    char *from_absolute = mk_executor_absolute(from);
    char *to_absolute = mk_executor_absolute(to);
    char *from_parent = from_absolute
        ? mk_executor_parent(from_absolute) : NULL;
    char *to_parent = to_absolute
        ? mk_executor_parent(to_absolute) : NULL;
    CCBOOL ok = CCFALSE;
    CCFiles *fs = NULL;
    char *from_vpath = NULL;
    char *to_vpath = NULL;
    if (!from_absolute || !to_absolute ||
        !from_parent || !to_parent)
        goto Done;
    fs = ccfiles_create();
    if (!fs)
        goto Done;
    if (ccfiles_mount(
            fs, "/s", from_parent, CC_FS_MOUNT_DIR
        ) != CC_FS_OK)
        goto Done;
    const char *to_mount = "/s";
    if (strcmp(from_parent, to_parent) != 0)
    {
        if (ccfiles_mount(
                fs, "/d", to_parent, CC_FS_MOUNT_DIR
            ) != CC_FS_OK)
            goto Done;
        to_mount = "/d";
    }
    from_vpath = mk_executor_vpath(
        "/s", mk_executor_base(from_absolute)
    );
    to_vpath = mk_executor_vpath(
        to_mount, mk_executor_base(to_absolute)
    );
    if (!from_vpath || !to_vpath)
        goto Done;
    CCINT32 result = move
        ? ccfiles_mv(fs, from_vpath, to_vpath)
        : ccfiles_cp(fs, from_vpath, to_vpath);
    ok = result == CC_FS_OK ? CCTRUE : CCFALSE;

Done:
    if (from_vpath)
        ccalloc(from_vpath, 0, NULL);
    if (to_vpath)
        ccalloc(to_vpath, 0, NULL);
    if (fs)
        ccfiles_destroy(fs);
    if (from_parent)
        ccalloc(from_parent, 0, NULL);
    if (to_parent)
        ccalloc(to_parent, 0, NULL);
    if (from_absolute)
        ccalloc(from_absolute, 0, NULL);
    if (to_absolute)
        ccalloc(to_absolute, 0, NULL);
    return ok;
}

static CCBOOL mk_executor_remove(const char *path)
{
    char *absolute = mk_executor_absolute(path);
    char *parent = absolute ? mk_executor_parent(absolute) : NULL;
    char *vpath = NULL;
    CCBOOL ok = CCFALSE;
    CCFiles *fs = NULL;
    if (!absolute || !parent)
        goto Done;
    fs = ccfiles_create();
    if (!fs ||
        ccfiles_mount(
            fs, "/m", parent, CC_FS_MOUNT_DIR
        ) != CC_FS_OK)
        goto Done;
    vpath = mk_executor_vpath("/m", mk_executor_base(absolute));
    if (!vpath)
        goto Done;
    CCINT32 result = ccfiles_remove(fs, vpath);
    ok = result == CC_FS_OK ? CCTRUE : CCFALSE;

Done:
    if (vpath)
        ccalloc(vpath, 0, NULL);
    if (fs)
        ccfiles_destroy(fs);
    if (parent)
        ccalloc(parent, 0, NULL);
    if (absolute)
        ccalloc(absolute, 0, NULL);
    return ok;
}

static CCBOOL mk_executor_file_action(const MKAction *action)
{
    switch (action->kind)
    {
    case MK_ACTION_COPY:
        return mk_executor_transfer(
            action->from.data, action->to.data, CCFALSE
        );
    case MK_ACTION_MOVE:
        return mk_executor_transfer(
            action->from.data, action->to.data, CCTRUE
        );
    case MK_ACTION_REMOVE:
        return mk_executor_remove(action->path.data);
    case MK_ACTION_MKDIR:
        return mk_executor_mkdir(action->path.data);
    default:
        return CCFALSE;
    }
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
