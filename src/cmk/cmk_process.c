/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 进程抽象（M7）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cmk_process.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <string.h>

#define CMK_PROCESS_COMMENT CC_DBG_REL_TAG(".cmk 进程", NULL)

struct cmk_process {
#ifdef CC_WINDOWS
    void *process_handle;
    void *read_handle;
    void *write_handle;
    void *job_handle;
#else
    int pid;
    int read_fd;
#endif
    CCBOOL started;
    CCBOOL waited;
    CMKProcessSpec spec;
};

static CCBOOL cmk_process_append(
    CMKString *output, const char *data, CCUINT32 len
)
{
    return cmk_string_append(output, data, len);
}

#ifdef CC_WINDOWS

#include <windows.h>

static void cmk_process_quote(
    CMKString *command, const char *text
)
{
    CCBOOL need = (text[0] == '\0');
    for (const char *p = text; *p; p++)
    {
        if (*p == ' ' || *p == '\t' || *p == '"')
            need = CCTRUE;
    }
    if (!need)
    {
        cmk_string_append(command, text, (CCUINT32)strlen(text));
        return;
    }
    cmk_string_append(command, "\"", 1);
    CCUINT32 backslashes = 0;
    for (const char *p = text; *p; p++)
    {
        if (*p == '\\')
        {
            backslashes++;
            continue;
        }
        if (*p == '"')
        {
            for (CCUINT32 i = 0; i < backslashes * 2 + 1; i++)
                cmk_string_append(command, "\\", 1);
            cmk_string_append(command, "\"", 1);
        }
        else
        {
            for (CCUINT32 i = 0; i < backslashes; i++)
                cmk_string_append(command, "\\", 1);
            cmk_string_append(command, p, 1);
        }
        backslashes = 0;
    }
    for (CCUINT32 i = 0; i < backslashes * 2; i++)
        cmk_string_append(command, "\\", 1);
    cmk_string_append(command, "\"", 1);
}

CMKProcess *cmk_process_create(const CMKProcessSpec *spec)
{
    if (!spec || !spec->program)
        return NULL;
    CMKProcess *process = ccalloc(
        NULL, sizeof(CMKProcess), CMK_PROCESS_COMMENT
    );
    if (!process)
        return NULL;
    memset(process, 0, sizeof(*process));
    process->spec = *spec;
    return process;
}

CCBOOL cmk_process_start(CMKProcess *process)
{
    if (!process || process->started)
        return CCFALSE;
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_handle = NULL;
    HANDLE write_handle = NULL;
    if (!CreatePipe(&read_handle, &write_handle, &sa, 0))
        return CCFALSE;
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

    CMKString command;
    cmk_string_init(&command);
    cmk_process_quote(&command, process->spec.program);
    for (CCUINT32 i = 0; process->spec.argv &&
         process->spec.argv[i]; i++)
    {
        cmk_string_append(&command, " ", 1);
        cmk_process_quote(&command, process->spec.argv[i]);
    }

    CCDyn env;
    memset(&env, 0, sizeof(env));
    char *env_block = NULL;
    if (process->spec.envp)
    {
        for (CCUINT32 i = 0; process->spec.envp[i]; i++)
        {
            const char *item = process->spec.envp[i];
            CCUINT32 len = (CCUINT32)strlen(item) + 1;
            if (!cmk_dyn_push(
                    &env, 1, (CCINT32)len, item
                ))
            {
                cmk_string_clear(&command);
                CloseHandle(read_handle);
                CloseHandle(write_handle);
                return CCFALSE;
            }
        }
        char zero = '\0';
        if (!cmk_dyn_push(&env, 1, 1, &zero))
        {
            if (env.array)
                ccalloc(env.array, 0, NULL);
            cmk_string_clear(&command);
            CloseHandle(read_handle);
            CloseHandle(write_handle);
            return CCFALSE;
        }
        env_block = (char *)env.array;
    }

    STARTUPINFOA startup;
    PROCESS_INFORMATION info;
    memset(&startup, 0, sizeof(startup));
    memset(&info, 0, sizeof(info));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_handle;
    startup.hStdError = write_handle;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        memset(&limits, 0, sizeof(limits));
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(
            job, JobObjectExtendedLimitInformation,
            &limits, sizeof(limits)
        );
    }

    BOOL ok = CreateProcessA(
        NULL, command.data, NULL, NULL, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, env_block,
        process->spec.working_dir, &startup, &info
    );
    if (env_block)
        ccalloc(env_block, 0, NULL);
    cmk_string_clear(&command);
    if (!ok)
    {
        if (job)
            CloseHandle(job);
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return CCFALSE;
    }
    if (job && !AssignProcessToJobObject(job, info.hProcess))
    {
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        CloseHandle(job);
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return CCFALSE;
    }
    ResumeThread(info.hThread);
    CloseHandle(write_handle);
    CloseHandle(info.hThread);
    process->process_handle = info.hProcess;
    process->read_handle = read_handle;
    process->job_handle = job;
    process->started = CCTRUE;
    return CCTRUE;
}

CCBOOL cmk_process_wait(CMKProcess *process, CMKProcessResult *out)
{
    if (!process || !process->started || !out || process->waited)
        return CCFALSE;
    memset(out, 0, sizeof(*out));
    cmk_string_init(&out->output);
    WaitForSingleObject((HANDLE)process->process_handle, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess((HANDLE)process->process_handle, &code);
    out->started = CCTRUE;
    out->exited = CCTRUE;
    out->exit_code = (CCINT32)code;
    if (process->job_handle)
        TerminateJobObject((HANDLE)process->job_handle, 0);

    char buffer[4096];
    DWORD read = 0;
    CCUINT64 total = 0;
    while (ReadFile(
               (HANDLE)process->read_handle, buffer,
               sizeof(buffer), &read, NULL
           ) && read > 0)
    {
        if (process->spec.capture_limit != 0 &&
            total + read > process->spec.capture_limit)
        {
            CCUINT64 remain = process->spec.capture_limit - total;
            if (remain > 0)
                cmk_process_append(&out->output, buffer, (CCUINT32)remain);
            out->capture_failed = CCTRUE;
            break;
        }
        if (!cmk_process_append(&out->output, buffer, read))
        {
            out->capture_failed = CCTRUE;
            break;
        }
        total += read;
    }
    CloseHandle((HANDLE)process->read_handle);
    CloseHandle((HANDLE)process->process_handle);
    if (process->job_handle)
    {
        CloseHandle((HANDLE)process->job_handle);
        process->job_handle = NULL;
    }
    process->read_handle = NULL;
    process->process_handle = NULL;
    process->waited = CCTRUE;
    return CCTRUE;
}

void cmk_process_terminate(CMKProcess *process)
{
    if (!process || !process->started || process->waited)
        return;
    if (process->job_handle)
        TerminateJobObject((HANDLE)process->job_handle, 1);
    else
        TerminateProcess((HANDLE)process->process_handle, 1);
}

void cmk_process_destroy(CMKProcess *process)
{
    if (!process)
        return;
    if (!process->waited)
    {
        if (process->process_handle)
        {
            if (process->job_handle)
                TerminateJobObject((HANDLE)process->job_handle, 1);
            else
                TerminateProcess((HANDLE)process->process_handle, 1);
            WaitForSingleObject((HANDLE)process->process_handle, 1000);
            CloseHandle((HANDLE)process->process_handle);
        }
        if (process->job_handle)
            CloseHandle((HANDLE)process->job_handle);
        if (process->read_handle)
            CloseHandle((HANDLE)process->read_handle);
        if (process->write_handle)
            CloseHandle((HANDLE)process->write_handle);
    }
    ccalloc(process, 0, NULL);
}

#else

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef CC_LINUX
/* glibc 需要 _GNU_SOURCE，这里直接声明以免改动本文件的特性宏。
 * 新版 glibc 的 environ 同样仅在 _GNU_SOURCE 下由 <unistd.h> 暴露。 */
extern char **environ;
extern int execvpe(
    const char *file, char *const argv[], char *const envp[]
);
#endif

/* 合并继承的环境与 spec 中的覆盖项；返回 NULL 表示分配失败。
 * 环境数组必须在 fork 前构建：子进程只允许调用异步信号安全函数。 */
static char **cmk_process_build_env(const CMKProcessSpec *spec)
{
    CCUINT32 base = 0;
    while (environ && environ[base])
        base++;
    CCUINT32 extra = 0;
    while (spec->envp && spec->envp[extra])
        extra++;

    char **envp = ccalloc(
        NULL, ((CCUINT64)base + extra + 1) * sizeof(char *),
        CMK_PROCESS_COMMENT
    );
    if (!envp)
        return NULL;

    CCUINT32 count = 0;
    for (CCUINT32 i = 0; i < base; i++)
    {
        const char *item = environ[i];
        const char *eq = strchr(item, '=');
        CCBOOL overridden = CCFALSE;
        if (eq)
        {
            CCUINT32 key_len = (CCUINT32)(eq - item);
            for (CCUINT32 j = 0; j < extra; j++)
            {
                const char *cover = spec->envp[j];
                if (cover && strchr(cover, '=') &&
                    strncmp(cover, item, key_len) == 0 &&
                    cover[key_len] == '=')
                {
                    overridden = CCTRUE;
                    break;
                }
            }
        }
        if (!overridden)
            envp[count++] = (char *)item;
    }
    for (CCUINT32 j = 0; j < extra; j++)
    {
        const char *item = spec->envp[j];
        const char *eq = item ? strchr(item, '=') : NULL;
        if (!eq)
            continue;
        CCUINT32 key_len = (CCUINT32)(eq - item);
        CCBOOL shadowed = CCFALSE;
        for (CCUINT32 k = j + 1; k < extra; k++)
        {
            const char *later = spec->envp[k];
            if (later && strchr(later, '=') &&
                strncmp(later, item, key_len) == 0 &&
                later[key_len] == '=')
            {
                shadowed = CCTRUE;
                break;
            }
        }
        if (!shadowed)
            envp[count++] = (char *)item;
    }
    envp[count] = NULL;
    return envp;
}

CMKProcess *cmk_process_create(const CMKProcessSpec *spec)
{
    if (!spec || !spec->program)
        return NULL;
    CMKProcess *process = ccalloc(
        NULL, sizeof(CMKProcess), CMK_PROCESS_COMMENT
    );
    if (!process)
        return NULL;
    memset(process, 0, sizeof(*process));
    process->spec = *spec;
    process->read_fd = -1;
    return process;
}

CCBOOL cmk_process_start(CMKProcess *process)
{
    if (!process || process->started)
        return CCFALSE;

    CCUINT32 argc = 0;
    while (process->spec.argv && process->spec.argv[argc])
        argc++;
    char **argv = ccalloc(
        NULL, (CCUINT64)(argc + 2) * sizeof(char *),
        CMK_PROCESS_COMMENT
    );
    if (!argv)
        return CCFALSE;
    argv[0] = (char *)process->spec.program;
    for (CCUINT32 i = 0; i < argc; i++)
        argv[i + 1] = (char *)process->spec.argv[i];
    argv[argc + 1] = NULL;

    char **envp = NULL;
    if (process->spec.envp)
    {
        envp = cmk_process_build_env(&process->spec);
        if (!envp)
        {
            ccalloc(argv, 0, NULL);
            return CCFALSE;
        }
    }

    int fds[2];
    if (pipe(fds) != 0)
    {
        if (envp)
            ccalloc(envp, 0, NULL);
        ccalloc(argv, 0, NULL);
        return CCFALSE;
    }
    pid_t pid = fork();
    if (pid < 0)
    {
        close(fds[0]);
        close(fds[1]);
        if (envp)
            ccalloc(envp, 0, NULL);
        ccalloc(argv, 0, NULL);
        return CCFALSE;
    }
    if (pid == 0)
    {
        setpgid(0, 0);
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        if (process->spec.working_dir &&
            chdir(process->spec.working_dir) != 0)
            _exit(127);
        if (envp)
            execvpe(process->spec.program, argv, envp);
        else
            execvp(process->spec.program, argv);
        _exit(127);
    }
    close(fds[1]);
    if (envp)
        ccalloc(envp, 0, NULL);
    ccalloc(argv, 0, NULL);
    setpgid(pid, pid);
    process->pid = (int)pid;
    process->read_fd = fds[0];
    process->started = CCTRUE;
    return CCTRUE;
}

CCBOOL cmk_process_wait(CMKProcess *process, CMKProcessResult *out)
{
    if (!process || !process->started || !out || process->waited)
        return CCFALSE;
    memset(out, 0, sizeof(*out));
    cmk_string_init(&out->output);
    int status = 0;
    if (waitpid((pid_t)process->pid, &status, 0) < 0)
        return CCFALSE;
    kill(-(pid_t)process->pid, SIGKILL);
    out->started = CCTRUE;
    out->exited = CCTRUE;
    if (WIFEXITED(status))
        out->exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        out->signal = WTERMSIG(status);

    char buffer[4096];
    ssize_t got = 0;
    CCUINT64 total = 0;
    while ((got = read(process->read_fd, buffer, sizeof(buffer))) > 0)
    {
        if (process->spec.capture_limit != 0 &&
            total + (CCUINT64)got > process->spec.capture_limit)
        {
            CCUINT64 remain = process->spec.capture_limit - total;
            if (remain > 0)
                cmk_process_append(
                    &out->output, buffer, (CCUINT32)remain
                );
            out->capture_failed = CCTRUE;
            break;
        }
        if (!cmk_process_append(
                &out->output, buffer, (CCUINT32)got
            ))
        {
            out->capture_failed = CCTRUE;
            break;
        }
        total += (CCUINT64)got;
    }
    close(process->read_fd);
    process->read_fd = -1;
    process->waited = CCTRUE;
    return CCTRUE;
}

void cmk_process_terminate(CMKProcess *process)
{
    if (!process || !process->started || process->waited)
        return;
    kill(-(pid_t)process->pid, SIGTERM);
    kill((pid_t)process->pid, SIGTERM);
}

void cmk_process_destroy(CMKProcess *process)
{
    if (!process)
        return;
    if (!process->waited)
    {
        if (process->started)
        {
            kill(-(pid_t)process->pid, SIGKILL);
            kill((pid_t)process->pid, SIGKILL);
            waitpid((pid_t)process->pid, NULL, 0);
        }
        if (process->read_fd >= 0)
            close(process->read_fd);
    }
    ccalloc(process, 0, NULL);
}

#endif

void cmk_process_result_clear(CMKProcessResult *result)
{
    if (!result)
        return;
    cmk_string_clear(&result->output);
    memset(result, 0, sizeof(*result));
}
