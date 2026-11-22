/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 内建函数注册表与调用接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_BUILTIN_H_
#define _INCLUDE_CMK_BUILTIN_H_

#include "cmk_diagnostic.h"
#include "cmk_value.h"

#define CMK_BUILTIN_ABI_VERSION 1u

typedef struct cmk_runtime CMKRuntime;

typedef enum cmk_builtin_flags {
    CMK_BUILTIN_PURE         = 1u << 0,
    CMK_BUILTIN_READ_FS      = 1u << 1,
    CMK_BUILTIN_QUERY_HOST   = 1u << 2,
    CMK_BUILTIN_BUILD_ACTION = 1u << 3,
    CMK_BUILTIN_VARIADIC     = 1u << 4,
    CMK_BUILTIN_NORETURN     = 1u << 5
} CMKBuiltinFlags;

/*
 * type 为参数类型。list 参数可将 element 设为 CMK_VALUE_VOID 表示“任意 list”；
 * 其余类型必须精确匹配。
 */
typedef struct cmk_builtin_arg {
    const char *name;
    CMKType type;
    CCBOOL optional;
} CMKBuiltinArg;

typedef struct cmk_builtin_call {
    CMKRuntime *runtime;       /* 不透明运行时，可为 NULL（单元测试） */
    CMKValue *args;            /* 借用，处理函数不得释放 */
    CCUINT32 argc;
    CMKValue *out;             /* 由运行时初始化；成功时写入拥有值 */
    CMKDiagnostic *diagnostic; /* 失败时写入位置和说明 */
    const char *file;
    CCUINT32 line;
    CCUINT32 col;
} CMKBuiltinCall;

typedef CCBOOL (*CMKBuiltinFn)(CMKBuiltinCall *call);

/* Runtime 侧接口；标准内建 error() 用它在成功后终止 Configure */
void cmk_runtime_mark_fatal(CMKRuntime *runtime);

typedef struct cmk_builtin_spec {
    CCUINT32 abi_version;
    const char *name;
    const char *signature;     /* 诊断展示 */
    const CMKBuiltinArg *args;
    CCUINT32 min_args;
    CCUINT32 max_args;
    CMKType return_type;
    CCUINT32 flags;
    CMKBuiltinFn fn;
} CMKBuiltinSpec;

typedef struct cmk_builtin_registry CMKBuiltinRegistry;

CMKBuiltinRegistry *cmk_builtin_registry_create(void);
void cmk_builtin_registry_destroy(CMKBuiltinRegistry *registry);
CCBOOL cmk_builtin_register(
    CMKBuiltinRegistry *registry, const CMKBuiltinSpec *spec
);
const CMKBuiltinSpec *cmk_builtin_find(
    const CMKBuiltinRegistry *registry, const char *name, CCUINT32 len
);
CCBOOL cmk_builtin_register_standard(CMKBuiltinRegistry *registry);

/* 写入调用诊断；返回 CCFALSE，便于 return cmk_builtin_error(...) */
CCBOOL cmk_builtin_error(CMKBuiltinCall *call, const char *fmt, ...);

/* 参数类型是否满足形式参数（list<VOID> 为通配列表） */
CCBOOL cmk_builtin_arg_matches(
    const CMKType *formal, const CMKType *actual
);

#endif
