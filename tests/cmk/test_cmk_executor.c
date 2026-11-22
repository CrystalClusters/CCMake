/*
 * @Description: CMK Executor 文件动作与进程动作测试（M7）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_executor.h"

#include <string.h>

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

    CMKPlanTarget target;
    cmk_plan_target_init(&target);
    CMKPlanAction action;
    cmk_plan_action_init(&action);

    action.type = CMK_PLAN_ACTION_MKDIR;
    cmk_string_set(&action.path, "out/cmk_exec_test", 17);
    CC_CHECK(
        cmk_plan_target_add_action(&target, &action), "追加 mkdir"
    );
    cmk_plan_action_clear(&action);

    cmk_plan_action_init(&action);
    action.type = CMK_PLAN_ACTION_COPY;
    cmk_string_set(&action.from, "out/cmk_exec_source", 19);
    cmk_string_set(&action.to, "out/cmk_exec_test/dest", 22);
    CC_CHECK(
        cmk_plan_target_add_action(&target, &action), "追加 copy"
    );
    cmk_plan_action_clear(&action);

    cmk_plan_action_init(&action);
    action.type = CMK_PLAN_ACTION_MOVE;
    cmk_string_set(&action.from, "out/cmk_exec_test/dest", 22);
    cmk_string_set(&action.to, "out/cmk_exec_test/moved", 23);
    CC_CHECK(
        cmk_plan_target_add_action(&target, &action), "追加 move"
    );
    cmk_plan_action_clear(&action);

    cmk_plan_action_init(&action);
    action.type = CMK_PLAN_ACTION_REMOVE;
    cmk_string_set(&action.path, "out/cmk_exec_test/moved", 23);
    CC_CHECK(
        cmk_plan_target_add_action(&target, &action), "追加 remove 文件"
    );
    cmk_plan_action_clear(&action);

    cmk_plan_action_init(&action);
    action.type = CMK_PLAN_ACTION_REMOVE;
    cmk_string_set(&action.path, "out/cmk_exec_test", 17);
    CC_CHECK(
        cmk_plan_target_add_action(&target, &action), "追加 remove 目录"
    );
    cmk_plan_action_clear(&action);

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKExecutor *executor = cmk_executor_create(NULL, &diagnostic);
    CC_CHECK(executor != NULL, "Executor 创建");
    CMKExecutorResult result;
    CC_CHECK(
        cmk_executor_ops()->run(executor, &target, &result),
        "文件动作执行"
    );
    CC_CHECK(result.success, "文件动作成功");
    CC_CHECK(remove("out/cmk_exec_source") == 0, "清理源文件");
    CC_CHECK(remove("out/cmk_exec_test") != 0, "目录已删除");

    cmk_plan_target_clear(&target);
    cmk_executor_destroy(executor);
    cmk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

static void exec_case_process(void)
{
    CC_CASE("EXEC-002", "进程动作");
    CMKPlanTarget target;
    cmk_plan_target_init(&target);
    CMKPlanAction action;
    cmk_plan_action_init(&action);
    action.type = CMK_PLAN_ACTION_EXEC;
#ifdef CC_WINDOWS
    cmk_string_set(&action.program, "cmd.exe", 7);
    cmk_plan_action_append_arg(&action, "/c", 2);
    cmk_plan_action_append_arg(&action, "exit", 4);
    cmk_plan_action_append_arg(&action, "0", 1);
#else
    cmk_string_set(&action.program, "/bin/sh", 7);
    cmk_plan_action_append_arg(&action, "-c", 2);
    cmk_plan_action_append_arg(&action, "exit 0", 6);
#endif
    CC_CHECK(
        cmk_plan_target_add_action(&target, &action), "追加 exec"
    );
    cmk_plan_action_clear(&action);

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKExecutor *executor = cmk_executor_create(NULL, &diagnostic);
    CMKExecutorResult result;
    CC_CHECK(
        cmk_executor_ops()->run(executor, &target, &result),
        "进程动作执行"
    );
    CC_CHECK(result.success, "进程动作成功");
    CC_CHECK_EQ_I64("进程退出码", 0, (CCINT64)result.exit_code);

    cmk_plan_target_clear(&target);
    cmk_executor_destroy(executor);
    cmk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

void test_cmk_executor(void)
{
    exec_case_files();
    exec_case_process();
}
