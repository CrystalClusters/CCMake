/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 语义分析：作用域、类型、函数、内建、target 与配置引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_SEMANTIC_H_
#define _INCLUDE_CMK_SEMANTIC_H_

#include "cmk_ast.h"
#include "cmk_builtin.h"
#include "cmk_config.h"
#include "cmk_value.h"

typedef struct cmk_semantic_module CMKSemanticModule;
typedef struct cmk_function_info CMKFunctionInfo;
typedef struct cmk_target_info CMKTargetInfo;

/* 内部布局见 cmk_semantic_internal.h；普通调用方只使用不透明指针和查询接口。 */

CMKSemanticModule *cmk_semantic_module_create(
    CMKAST *ast, const char *path,
    CMKConfig *config, CMKBuiltinRegistry *builtins,
    CMKDiagnostic *diagnostic
);
void cmk_semantic_module_destroy(CMKSemanticModule *module);

/* 由 Program 在 analyze 前加入已加载模块的导入符号 */
CCBOOL cmk_semantic_add_imported_function(
    CMKSemanticModule *module,
    const char *name, CCUINT32 name_len,
    const CMKFunctionInfo *function
);
CCBOOL cmk_semantic_add_imported_target(
    CMKSemanticModule *module,
    const char *alias, CCUINT32 alias_len,
    const CMKTargetInfo *target
);

/* 全模块语义检查；失败时 module->diagnostic 已写入 */
CCBOOL cmk_semantic_analyze(CMKSemanticModule *module);

const CMKAST *cmk_semantic_ast(const CMKSemanticModule *module);
const char *cmk_semantic_path(const CMKSemanticModule *module);

const CMKType *cmk_semantic_node_type(
    const CMKSemanticModule *module, CMKASTNodeId node
);
const CMKTargetInfo *cmk_semantic_node_target_ref(
    const CMKSemanticModule *module, CMKASTNodeId node
);
const CMKFunctionInfo *cmk_semantic_node_function(
    const CMKSemanticModule *module, CMKASTNodeId node
);
const CMKBuiltinSpec *cmk_semantic_node_builtin(
    const CMKSemanticModule *module, CMKASTNodeId node
);
CCBOOL cmk_semantic_node_is_noreturn(
    const CMKSemanticModule *module, CMKASTNodeId node
);

const CMKFunctionInfo *cmk_semantic_find_function(
    const CMKSemanticModule *module, const char *name, CCUINT32 len
);
const CMKTargetInfo *cmk_semantic_find_target(
    const CMKSemanticModule *module, const char *name, CCUINT32 len
);
const CMKTargetInfo *cmk_semantic_find_target_by_decl(
    const CMKSemanticModule *module, CMKASTNodeId decl
);
CCUINT32 cmk_semantic_function_count(const CMKSemanticModule *module);
const CMKFunctionInfo *cmk_semantic_function_at(
    const CMKSemanticModule *module, CCUINT32 index
);
CCUINT32 cmk_semantic_target_count(const CMKSemanticModule *module);
const CMKTargetInfo *cmk_semantic_target_at(
    const CMKSemanticModule *module, CCUINT32 index
);

#endif
