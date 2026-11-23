/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK Configure 求值运行时（M4）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_RUNTIME_H_
#define _INCLUDE_CMK_RUNTIME_H_

#include "cmk_builtin.h"
#include "cmk_config.h"
#include "cmk_policy.h"
#include "cmk_semantic.h"
#include "cmk_target_draft.h"

typedef struct cmk_runtime CMKRuntime;

/* 跨模块函数调用时由 Program 提供 module -> runtime 映射 */
typedef CMKRuntime *(*CMKRuntimeModuleResolver)(
    CMKRuntime *runtime, CMKSemanticModule *module
);

CMKRuntime *cmk_runtime_create(
    CMKSemanticModule *semantic,
    CMKConfig *config,
    CMKBuiltinRegistry *builtins,
    CMKDiagnostic *diagnostic
);
void cmk_runtime_destroy(CMKRuntime *runtime);

/* 初始化模块全局变量；Program 在跨模块调用前按依赖序调用 */
CCBOOL cmk_runtime_init_globals(CMKRuntime *runtime);

/* 初始化模块全局变量，并求值顶层 target / 顶层 if 到 target drafts */
CCBOOL cmk_runtime_configure(CMKRuntime *runtime);
CCBOOL cmk_runtime_is_configured(const CMKRuntime *runtime);
CCBOOL cmk_runtime_failed(const CMKRuntime *runtime);

void cmk_runtime_set_module_resolver(
    CMKRuntime *runtime,
    CMKRuntimeModuleResolver resolver, void *user
);
void *cmk_runtime_get_resolver_user(const CMKRuntime *runtime);
/* Program 注入工作目录，供 find_files 等只读内建解析相对路径 */
void cmk_runtime_set_work_dir(CMKRuntime *runtime, const char *work_dir);
const char *cmk_runtime_get_work_dir(const CMKRuntime *runtime);
void cmk_runtime_set_policy(
    CMKRuntime *runtime, const CMKExecutionPolicy *policy
);
const CMKExecutionPolicy *cmk_runtime_get_policy(
    const CMKRuntime *runtime
);

CCUINT32 cmk_runtime_target_count(const CMKRuntime *runtime);
const CMKTargetDraft *cmk_runtime_target_at(
    const CMKRuntime *runtime, CCUINT32 index
);

/* 读取模块级变量（Configure 完成后有效），out 由调用方初始化 */
CCBOOL cmk_runtime_get_global(
    const CMKRuntime *runtime,
    const char *name, CCUINT32 name_len,
    CMKValue *out
);

/* 供测试和 Program 调用；参数值按值复制，out 由调用方初始化 */
CCBOOL cmk_runtime_call_function(
    CMKRuntime *runtime,
    const CMKFunctionInfo *function,
    const CMKValue *args, CCUINT32 argc,
    CMKValue *out
);

/* 标准内建 error() 用来终止 Configure */
void cmk_runtime_mark_fatal(CMKRuntime *runtime);

#endif
