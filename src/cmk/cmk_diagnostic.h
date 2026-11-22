/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .cmk 词法、语法与语义阶段共用的诊断对象
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_DIAGNOSTIC_H_
#define _INCLUDE_CMK_DIAGNOSTIC_H_

#include "cmk_source.h"

#include <stdarg.h>

typedef enum {
    CMK_DIAG_ERROR = 0,
    CMK_DIAG_WARNING,
    CMK_DIAG_INFO
} CMKDiagnosticLevel;

/* 一条诊断；file 与 message 由本结构持有，调用方通过 clear 释放 */
typedef struct cmk_diagnostic {
    CMKDiagnosticLevel level;
    char *file;
    CCUINT32 line;
    CCUINT32 col;
    char *message;
} CMKDiagnostic;

void cmk_diagnostic_init(CMKDiagnostic *diag);
void cmk_diagnostic_clear(CMKDiagnostic *diag);

/*
 * 覆盖写入一条诊断。file 只读，内部复制；fmt 与可变参数生成 message。
 * 失败时返回 CCFALSE，已持有的旧诊断保持不变。
 */
CCBOOL cmk_diagnostic_set(
    CMKDiagnostic *diag, CMKDiagnosticLevel level,
    const char *file, CCUINT32 line, CCUINT32 col,
    const char *fmt, ...
);

CCBOOL cmk_diagnostic_vset(
    CMKDiagnostic *diag, CMKDiagnosticLevel level,
    const char *file, CCUINT32 line, CCUINT32 col,
    const char *fmt, va_list ap
);

CCBOOL cmk_diagnostic_has(const CMKDiagnostic *diag);

/*
 * 按前缀输出诊断。prefix 为 NULL 时使用“构建脚本”。
 * 有文件与行号时输出 <文件>:<行>[:<列>]，否则只输出消息。
 */
void cmk_diagnostic_print(const CMKDiagnostic *diag, const char *prefix);

#endif
