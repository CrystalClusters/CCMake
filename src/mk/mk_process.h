/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 进程抽象
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_PROCESS_H_
#define _INCLUDE_MK_PROCESS_H_

#include "mk_value.h"

typedef struct mk_process MKProcess;

typedef struct mk_process_spec {
    const char *program;
    const char *const *argv;  /* 不含 argv[0]；可 NULL */
    const char *const *envp;  /* "K=V"；NULL 表示继承 */
    const char *working_dir;  /* 可 NULL */
    CCUINT64 capture_limit;   /* 0 = 不限制捕获 */
} MKProcessSpec;

typedef struct mk_process_result {
    CCBOOL started;
    CCBOOL exited;
    CCINT32 exit_code;
    CCINT32 signal;
    CCBOOL capture_failed;
    MKString output;
} MKProcessResult;

MKProcess *mk_process_create(const MKProcessSpec *spec);
CCBOOL mk_process_start(MKProcess *process);
/* 等待并回收；结果由调用方 mk_process_result_clear */
CCBOOL mk_process_wait(MKProcess *process, MKProcessResult *out);
void mk_process_terminate(MKProcess *process);
void mk_process_destroy(MKProcess *process);
void mk_process_result_clear(MKProcessResult *result);

#endif
