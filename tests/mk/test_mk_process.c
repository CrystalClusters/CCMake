/*
 * @Description: MK Process 测试（M7）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "mk/mk_process.h"

#include <string.h>

void test_mk_process(void)
{
#ifdef CC_WINDOWS
    const char *program = "cmd.exe";
    const char *args[] = {"/c", "echo", "hello", NULL};
#elif defined(CC_ANDROID)
    const char *program = "/system/bin/sh";
    const char *args[] = {"-c", "echo hello", NULL};
#else
    const char *program = "/bin/sh";
    const char *args[] = {"-c", "echo hello", NULL};
#endif

    CC_CASE("PROC-001", "进程输出与退出码");
    {
        MKProcessSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.program = program;
        spec.argv = args;
        MKProcess *process = mk_process_create(&spec);
        CC_CHECK(process != NULL, "进程创建");
        CC_CHECK(mk_process_start(process), "进程启动");
        MKProcessResult result;
        CC_CHECK(mk_process_wait(process, &result), "进程等待");
        CC_CHECK(result.exited, "进程退出");
        CC_CHECK_EQ_I64("退出码", 0, (CCINT64)result.exit_code);
        CC_CHECK(
            result.output.data &&
            strstr(result.output.data, "hello") != NULL,
            "输出捕获"
        );
        mk_process_result_clear(&result);
        mk_process_destroy(process);
    }
    CC_CASE_END();

    CC_CASE("PROC-002", "输出捕获上限");
    {
        MKProcessSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.program = program;
        spec.argv = args;
        spec.capture_limit = 2;
        MKProcess *process = mk_process_create(&spec);
        CC_CHECK(process != NULL, "进程创建");
        CC_CHECK(mk_process_start(process), "进程启动");
        MKProcessResult result;
        CC_CHECK(mk_process_wait(process, &result), "进程等待");
        CC_CHECK(result.capture_failed, "捕获超限被拒绝");
        CC_CHECK(
            result.output.len <= 2, "捕获长度受限"
        );
        mk_process_result_clear(&result);
        mk_process_destroy(process);
    }
    CC_CASE_END();
}
