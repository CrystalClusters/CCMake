/*
 * @Description: MK Executor 文件动作与进程动作测试
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "mk/mk_executor.h"

#include <string.h>

static CCBOOL action_add_arg(MKAction *action, const char *text)
{
    MKValue value;
    mk_value_init(&value);
    CCBOOL ok = mk_value_set_string(
        &value, text, (CCUINT32)strlen(text)
    );
    if (ok)
        ok = mk_list_append(action->args, &value);
    mk_value_clear(&value);
    return ok;
}

static void action_clear(MKAction *action)
{
    mk_string_clear(&action->program);
    mk_string_clear(&action->cwd);
    mk_string_clear(&action->from);
    mk_string_clear(&action->to);
    mk_string_clear(&action->path);
    if (action->args)
        mk_list_destroy(action->args);
    if (action->env)
        mk_list_destroy(action->env);
    memset(action, 0, sizeof(*action));
}

static void exec_case_files(void)
{
    CC_CASE("EXEC-001", "文件动作");
    remove("out/mk_exec_test/dest");
    remove("out/mk_exec_test/moved");
    remove("out/mk_exec_test");
    remove("out/mk_exec_source");
    FILE *file = fopen("out/mk_exec_source", "wb");
    CC_CHECK(file != NULL, "创建源文件");
    if (file)
    {
        fputs("payload", file);
        fclose(file);
    }

    MKAction actions[5];
    memset(actions, 0, sizeof(actions));

    actions[0].kind = MK_ACTION_MKDIR;
    mk_string_set(&actions[0].path, "out/mk_exec_test", 17);

    actions[1].kind = MK_ACTION_COPY;
    mk_string_set(&actions[1].from, "out/mk_exec_source", 19);
    mk_string_set(&actions[1].to, "out/mk_exec_test/dest", 22);

    actions[2].kind = MK_ACTION_MOVE;
    mk_string_set(&actions[2].from, "out/mk_exec_test/dest", 22);
    mk_string_set(&actions[2].to, "out/mk_exec_test/moved", 23);

    actions[3].kind = MK_ACTION_REMOVE;
    mk_string_set(&actions[3].path, "out/mk_exec_test/moved", 23);

    actions[4].kind = MK_ACTION_REMOVE;
    mk_string_set(&actions[4].path, "out/mk_exec_test", 17);

    MKDiagnostic diagnostic;
    mk_diagnostic_init(&diagnostic);
    MKExecutor *executor = mk_executor_create(NULL, &diagnostic);
    CC_CHECK(executor != NULL, "Executor 创建");
    MKExecutorResult result;
    CC_CHECK(
        mk_executor_run_actions(executor, actions, 5, &result),
        "文件动作执行"
    );
    CC_CHECK(result.success, "文件动作成功");
    CC_CHECK(remove("out/mk_exec_source") == 0, "清理源文件");
    CC_CHECK(remove("out/mk_exec_test") != 0, "目录已删除");

    for (CCUINT32 i = 0; i < 5; i++)
        action_clear(&actions[i]);
    mk_executor_destroy(executor);
    mk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

static void exec_case_process(void)
{
    CC_CASE("EXEC-002", "进程动作");
    MKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = MK_ACTION_EXEC;
    action.args = mk_list_create(MK_VALUE_STRING);
    CC_CHECK(action.args != NULL, "参数列表");
#ifdef CC_WINDOWS
    mk_string_set(&action.program, "cmd.exe", 7);
    CC_CHECK(action_add_arg(&action, "/c"), "参数 /c");
    CC_CHECK(action_add_arg(&action, "exit"), "参数 exit");
    CC_CHECK(action_add_arg(&action, "0"), "参数 0");
#elif defined(CC_ANDROID)
    mk_string_set(&action.program, "/system/bin/sh", 14);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "exit 0"), "参数 exit 0");
#else
    mk_string_set(&action.program, "/bin/sh", 7);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "exit 0"), "参数 exit 0");
#endif

    MKDiagnostic diagnostic;
    mk_diagnostic_init(&diagnostic);
    MKExecutor *executor = mk_executor_create(NULL, &diagnostic);
    MKExecutorResult result;
    CC_CHECK(
        mk_executor_run_actions(executor, &action, 1, &result),
        "进程动作执行"
    );
    CC_CHECK(result.success, "进程动作成功");
    CC_CHECK_EQ_I64("进程退出码", 0, (CCINT64)result.exit_code);

    mk_executor_result_clear(&result);
    action_clear(&action);
    mk_executor_destroy(executor);
    mk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

static void exec_case_output(void)
{
    CC_CASE("EXEC-003", "输出缓冲");
    MKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = MK_ACTION_EXEC;
    action.args = mk_list_create(MK_VALUE_STRING);
    CC_CHECK(action.args != NULL, "参数列表");
#ifdef CC_WINDOWS
    mk_string_set(&action.program, "cmd.exe", 7);
    CC_CHECK(action_add_arg(&action, "/c"), "参数 /c");
    CC_CHECK(action_add_arg(&action, "echo"), "参数 echo");
    CC_CHECK(action_add_arg(&action, "hello"), "参数 hello");
#elif defined(CC_ANDROID)
    mk_string_set(&action.program, "/system/bin/sh", 14);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "echo hello"), "参数 echo");
#else
    mk_string_set(&action.program, "/bin/sh", 7);
    CC_CHECK(action_add_arg(&action, "-c"), "参数 -c");
    CC_CHECK(action_add_arg(&action, "echo hello"), "参数 echo");
#endif

    MKDiagnostic diagnostic;
    mk_diagnostic_init(&diagnostic);
    MKExecutor *executor = mk_executor_create(NULL, &diagnostic);
    MKExecutorResult result;
    CC_CHECK(
        mk_executor_run_actions(executor, &action, 1, &result),
        "输出动作执行"
    );
    CC_CHECK(
        result.output.data &&
        strstr(result.output.data, "hello") != NULL,
        "输出已缓冲"
    );
    mk_executor_result_clear(&result);
    action_clear(&action);
    mk_executor_destroy(executor);
    mk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

void test_mk_executor(void)
{
    exec_case_files();
    exec_case_process();
    exec_case_output();
}
