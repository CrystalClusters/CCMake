/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .mk 词法、语法与语义阶段共用的诊断对象
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_DIAGNOSTIC_H_
#define _INCLUDE_MK_DIAGNOSTIC_H_

#include "mk_source.h"

#include <stdarg.h>

typedef enum {
    MK_DIAG_ERROR = 0,
    MK_DIAG_WARNING,
    MK_DIAG_INFO
} MKDiagnosticLevel;

/* 一条诊断；file 与 message 由本结构持有，调用方通过 clear 释放 */
typedef struct mk_diagnostic {
    MKDiagnosticLevel level;
    char *file;
    CCUINT32 line;
    CCUINT32 col;
    char *message;
} MKDiagnostic;

void mk_diagnostic_init(MKDiagnostic *diag);
void mk_diagnostic_clear(MKDiagnostic *diag);

/*
 * 覆盖写入一条诊断。file 只读，内部复制；fmt 与可变参数生成 message。
 * 失败时返回 CCFALSE，已持有的旧诊断保持不变。
 */
CCBOOL mk_diagnostic_set(
    MKDiagnostic *diag, MKDiagnosticLevel level,
    const char *file, CCUINT32 line, CCUINT32 col,
    const char *fmt, ...
);

CCBOOL mk_diagnostic_vset(
    MKDiagnostic *diag, MKDiagnosticLevel level,
    const char *file, CCUINT32 line, CCUINT32 col,
    const char *fmt, va_list ap
);

CCBOOL mk_diagnostic_has(const MKDiagnostic *diag);

/*
 * 按前缀输出诊断。prefix 为 NULL 时使用“构建脚本”。
 * 有文件与行号时输出 <文件>:<行>[:<列>]，否则只输出消息。
 */
void mk_diagnostic_print(const MKDiagnostic *diag, const char *prefix);

#endif
