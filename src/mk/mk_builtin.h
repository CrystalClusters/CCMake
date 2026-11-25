/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 内建函数注册表与调用接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_BUILTIN_H_
#define _INCLUDE_MK_BUILTIN_H_

#include "mk_diagnostic.h"
#include "mk_value.h"

#define MK_BUILTIN_ABI_VERSION 1u

typedef struct mk_runtime MKRuntime;

typedef enum mk_builtin_flags {
    MK_BUILTIN_PURE         = 1u << 0,
    MK_BUILTIN_READ_FS      = 1u << 1,
    MK_BUILTIN_QUERY_HOST   = 1u << 2,
    MK_BUILTIN_BUILD_ACTION = 1u << 3,
    MK_BUILTIN_VARIADIC     = 1u << 4,
    MK_BUILTIN_NORETURN     = 1u << 5
} MKBuiltinFlags;

/*
 * type 为参数类型。list 参数可将 element 设为 MK_VALUE_VOID 表示“任意 list”；
 * 其余类型必须精确匹配。
 */
typedef struct mk_builtin_arg {
    const char *name;
    MKType type;
    CCBOOL optional;
} MKBuiltinArg;

typedef struct mk_builtin_call {
    MKRuntime *runtime;       /* 不透明运行时，可为 NULL（单元测试） */
    MKValue *args;            /* 借用，处理函数不得释放 */
    CCUINT32 argc;
    MKValue *out;             /* 由运行时初始化；成功时写入拥有值 */
    MKDiagnostic *diagnostic; /* 失败时写入位置和说明 */
    const char *file;
    CCUINT32 line;
    CCUINT32 col;
} MKBuiltinCall;

typedef CCBOOL (*MKBuiltinFn)(MKBuiltinCall *call);

/* Runtime 侧接口；标准内建 error() 用它在成功后终止 Configure */
void mk_runtime_mark_fatal(MKRuntime *runtime);

typedef struct mk_builtin_spec {
    CCUINT32 abi_version;
    const char *name;
    const char *signature;     /* 诊断展示 */
    const MKBuiltinArg *args;
    CCUINT32 min_args;
    CCUINT32 max_args;
    MKType return_type;
    CCUINT32 flags;
    MKBuiltinFn fn;
} MKBuiltinSpec;

typedef struct mk_builtin_registry MKBuiltinRegistry;

MKBuiltinRegistry *mk_builtin_registry_create(void);
void mk_builtin_registry_destroy(MKBuiltinRegistry *registry);
CCBOOL mk_builtin_register(
    MKBuiltinRegistry *registry, const MKBuiltinSpec *spec
);
const MKBuiltinSpec *mk_builtin_find(
    const MKBuiltinRegistry *registry, const char *name, CCUINT32 len
);
CCBOOL mk_builtin_register_standard(MKBuiltinRegistry *registry);

/* 写入调用诊断；返回 CCFALSE，便于 return mk_builtin_error(...) */
CCBOOL mk_builtin_error(MKBuiltinCall *call, const char *fmt, ...);

/* 参数类型是否满足形式参数（list<VOID> 为通配列表） */
CCBOOL mk_builtin_arg_matches(
    const MKType *formal, const MKType *actual
);

#endif
