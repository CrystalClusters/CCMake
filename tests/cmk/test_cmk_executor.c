/*
 * @Description: CMK Executor 文件动作与进程动作测试
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_executor.h"

#include <string.h>

static CCBOOL action_add_arg(CMKAction *action, const char *text)
{
    CMKValue value;
    cmk_value_init(&value);
    CCBOOL ok = cmk_value_set_string(
        &value, text, (CCUINT32)strlen(text)
    );
    if (ok)
        ok = cmk_list_append(action->args, &value);
    cmk_value_clear(&value);
    return ok;
}

static void action_clear(CMKAction *action)
{
    cmk_string_clear(&action->program);
    cmk_string_clear(&action->cwd);
    cmk_string_clear(&action->from);
    cmk_string_clear(&action->to);
    cmk_string_clear(&action->path);
    if (action->args)
        cmk_list_destroy(action->args);
    if (action->env)
        cmk_list_destroy(action->env);
    memset(action, 0, sizeof(*action));
}

static void exec_case_files(void)
{
    CC_CASE("EXEC-001", "文件动作");
    remove("out/cmk_exec_test/dest");
    remove("out/cmk_exec_test/moved");
    remove("out/cmk_exec_test");
    remove("out/cmk_exec_source");
    FILE *file = fopen("out/cmk_exec_source", "wb");
    CC_CHECK(file != NULL, "创建源文件");
    if (file)
    {
        fputs("payload", file);
        fclose(file);
    }

    CMKAction actions[5];
    memset(actions, 0, sizeof(actions));

    actions[0].kind = CMK_ACTION_MKDIR;
    cmk_string_set(&actions[0].path, "out/cmk_exec_test", 17);

    actions[1].kind = CMK_ACTION_COPY;
    cmk_string_set(&actions[1].from, "out/cmk_exec_source", 19);
    cmk_string_set(&actions[1].to, "out/cmk_exec_test/dest", 22);

    actions[2].kind = CMK_ACTION_MOVE;
    cmk_string_set(&actions[2].from, "out/cmk_exec_test/dest", 22);
    cmk_string_set(&actions[2].to, "out/cmk_exec_test/moved", 23);

    actions[3].kind = CMK_ACTION_REMOVE;
    cmk_string_set(&actions[3].path, "out/cmk_exec_test/moved", 23);

    actions[4].kind = CMK_ACTION_REMOVE;
    cmk_string_set(&actions[4].path, "out/cmk_exec_test", 17);

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKExecutor *executor = cmk_executor_create(NULL, &diagnostic);
    CC_CHECK(executor != NULL, "Executor 创建");
    CMKExecutorResult result;
    CC_CHECK(
        cmk_executor_run_actions(executor, actions, 5, &result),
        "文件动作执行"
    );
    CC_CHECK(result.success, "文件动作成功");
    CC_CHECK(remove("out/cmk_exec_source") == 0, "清理源文件");
    CC_CHECK(remove("out/cmk_exec_test") != 0, "目录已删除");

    for (CCUINT32 i = 0; i < 5; i++)
        action_clear(&actions[i]);
    cmk_executor_destroy(executor);
    cmk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

static void exec_case_process(void)
{
    CC_CASE("EXEC-002", "进程动作");
    CMKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = CMK_ACTION_EXEC;
    action.args = cmk_list_create(CMK_VALUE_STRING);
    CC_CHECK(action.args != NULL, "参数列表");
#ifdef CC_WINDOWS
    cmk_string_set(&action.program, "cmd.exe", 7);
    CC_CHECK(action_add_arg(&action, "/c"), "参数 /c");
    CC_CHECK(action_add_arg(&action, "exit"), "参数 exit");
    CC_CHECK(action_add_arg(&action, "0"), "参数 0");
#elif defined(CC_ANDROID)
    cmk_string_set(&action.program, "/system/bin/sh", 14);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "exit 0"), "参数 exit 0");
#else
    cmk_string_set(&action.program, "/bin/sh", 7);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "exit 0"), "参数 exit 0");
#endif

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKExecutor *executor = cmk_executor_create(NULL, &diagnostic);
    CMKExecutorResult result;
    CC_CHECK(
        cmk_executor_run_actions(executor, &action, 1, &result),
        "进程动作执行"
    );
    CC_CHECK(result.success, "进程动作成功");
    CC_CHECK_EQ_I64("进程退出码", 0, (CCINT64)result.exit_code);

    cmk_executor_result_clear(&result);
    action_clear(&action);
    cmk_executor_destroy(executor);
    cmk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

static void exec_case_output(void)
{
    CC_CASE("EXEC-003", "输出缓冲");
    CMKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = CMK_ACTION_EXEC;
    action.args = cmk_list_create(CMK_VALUE_STRING);
    CC_CHECK(action.args != NULL, "参数列表");
#ifdef CC_WINDOWS
    cmk_string_set(&action.program, "cmd.exe", 7);
    CC_CHECK(action_add_arg(&action, "/c"), "参数 /c");
    CC_CHECK(action_add_arg(&action, "echo"), "参数 echo");
    CC_CHECK(action_add_arg(&action, "hello"), "参数 hello");
#elif defined(CC_ANDROID)
    cmk_string_set(&action.program, "/system/bin/sh", 14);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "echo hello"), "参数 echo");
#else
    cmk_string_set(&action.program, "/bin/sh", 7);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "echo hello"), "参数 echo");
#endif

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKExecutor *executor = cmk_executor_create(NULL, &diagnostic);
    CMKExecutorResult result;
    CC_CHECK(
        cmk_executor_run_actions(executor, &action, 1, &result),
        "输出动作执行"
    );
    CC_CHECK(
        result.output.data &&
        strstr(result.output.data, "hello") != NULL,
        "输出已缓冲"
    );
    cmk_executor_result_clear(&result);
    action_clear(&action);
    cmk_executor_destroy(executor);
    cmk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

void test_cmk_executor(void)
{
    exec_case_files();
    exec_case_process();
    exec_case_output();
}
