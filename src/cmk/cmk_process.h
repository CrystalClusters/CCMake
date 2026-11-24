/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 进程抽象
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_PROCESS_H_
#define _INCLUDE_CMK_PROCESS_H_

#include "cmk_value.h"

typedef struct cmk_process CMKProcess;

typedef struct cmk_process_spec {
    const char *program;
    const char *const *argv;  /* 不含 argv[0]；可 NULL */
    const char *const *envp;  /* "K=V"；NULL 表示继承 */
    const char *working_dir;  /* 可 NULL */
    CCUINT64 capture_limit;   /* 0 = 不限制捕获 */
} CMKProcessSpec;

typedef struct cmk_process_result {
    CCBOOL started;
    CCBOOL exited;
    CCINT32 exit_code;
    CCINT32 signal;
    CCBOOL capture_failed;
    CMKString output;
} CMKProcessResult;

CMKProcess *cmk_process_create(const CMKProcessSpec *spec);
CCBOOL cmk_process_start(CMKProcess *process);
/* 等待并回收；结果由调用方 cmk_process_result_clear */
CCBOOL cmk_process_wait(CMKProcess *process, CMKProcessResult *out);
void cmk_process_terminate(CMKProcess *process);
void cmk_process_destroy(CMKProcess *process);
void cmk_process_result_clear(CMKProcessResult *result);

#endif
