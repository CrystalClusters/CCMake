/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK Configure 求值运行时
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_RUNTIME_H_
#define _INCLUDE_MK_RUNTIME_H_

#include "mk_builtin.h"
#include "mk_config.h"
#include "mk_policy.h"
#include "mk_semantic.h"

typedef struct mk_runtime MKRuntime;

/* 跨模块函数调用时由 Program 提供 module -> runtime 映射 */
typedef MKRuntime *(*MKRuntimeModuleResolver)(
    MKRuntime *runtime, MKSemanticModule *module
);

MKRuntime *mk_runtime_create(
    MKSemanticModule *semantic,
    MKConfig *config,
    MKBuiltinRegistry *builtins,
    MKDiagnostic *diagnostic
);
void mk_runtime_destroy(MKRuntime *runtime);

/* 初始化模块全局变量；Program 在跨模块调用前按依赖序调用 */
CCBOOL mk_runtime_init_globals(MKRuntime *runtime);

/* 初始化模块全局变量 */
CCBOOL mk_runtime_configure(MKRuntime *runtime);
CCBOOL mk_runtime_is_configured(const MKRuntime *runtime);
CCBOOL mk_runtime_failed(const MKRuntime *runtime);

void mk_runtime_set_module_resolver(
    MKRuntime *runtime,
    MKRuntimeModuleResolver resolver, void *user
);
void *mk_runtime_get_resolver_user(const MKRuntime *runtime);
/* Program 注入工作目录，供 find_files 等只读内建解析相对路径 */
void mk_runtime_set_work_dir(MKRuntime *runtime, const char *work_dir);
const char *mk_runtime_get_work_dir(const MKRuntime *runtime);
void mk_runtime_set_policy(
    MKRuntime *runtime, const MKExecutionPolicy *policy
);
const MKExecutionPolicy *mk_runtime_get_policy(
    const MKRuntime *runtime
);

/* 读取模块级变量（Configure 完成后有效），out 由调用方初始化 */
CCBOOL mk_runtime_get_global(
    const MKRuntime *runtime,
    const char *name, CCUINT32 name_len,
    MKValue *out
);

/* 供测试和 Program 调用；参数值按值复制，out 由调用方初始化 */
CCBOOL mk_runtime_call_function(
    MKRuntime *runtime,
    const MKFunctionInfo *function,
    const MKValue *args, CCUINT32 argc,
    MKValue *out
);

/* 标准内建 error() 用来终止 Configure */
void mk_runtime_mark_fatal(MKRuntime *runtime);

#endif
