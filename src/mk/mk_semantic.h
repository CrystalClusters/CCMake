/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 语义分析：作用域、类型、函数、内建与配置引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_SEMANTIC_H_
#define _INCLUDE_MK_SEMANTIC_H_

#include "mk_ast.h"
#include "mk_builtin.h"
#include "mk_config.h"
#include "mk_value.h"

typedef struct mk_semantic_module MKSemanticModule;
typedef struct mk_function_info MKFunctionInfo;

/* 内部布局见 mk_semantic_internal.h；普通调用方只使用不透明指针和查询接口。 */

MKSemanticModule *mk_semantic_module_create(
    MKAST *ast, const char *path,
    MKConfig *config, MKBuiltinRegistry *builtins,
    MKDiagnostic *diagnostic
);
void mk_semantic_module_destroy(MKSemanticModule *module);

/* 由 Program 在 analyze 前加入已加载模块的导入符号 */
CCBOOL mk_semantic_add_imported_function(
    MKSemanticModule *module,
    const char *name, CCUINT32 name_len,
    const MKFunctionInfo *function
);
const MKFunctionInfo *mk_semantic_node_function(
    const MKSemanticModule *module, MKASTNodeId node
);
const MKBuiltinSpec *mk_semantic_node_builtin(
    const MKSemanticModule *module, MKASTNodeId node
);
CCBOOL mk_semantic_node_is_noreturn(
    const MKSemanticModule *module, MKASTNodeId node
);

const MKFunctionInfo *mk_semantic_find_function(
    const MKSemanticModule *module, const char *name, CCUINT32 len
);
CCUINT32 mk_semantic_function_count(const MKSemanticModule *module);
const MKFunctionInfo *mk_semantic_function_at(
    const MKSemanticModule *module, CCUINT32 index
);
CCBOOL mk_semantic_analyze(MKSemanticModule *module);
const MKType *mk_semantic_node_type(
    const MKSemanticModule *module, MKASTNodeId node
);
#endif
