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

typedef enum cmk_symbol_kind {
    CMK_SYMBOL_VAR = 0,
    CMK_SYMBOL_FUNCTION,
    CMK_SYMBOL_TARGET
} CMKSymbolKind;

typedef struct cmk_symbol {
    CMKString name;
    CMKSymbolKind kind;
    CMKType type;                 /* 变量类型或函数返回类型 */
    CCBOOL is_const;
    const CMKFunctionInfo *function;
    const CMKTargetInfo *target;
} CMKSymbol;

typedef struct cmk_scope {
    struct cmk_scope *parent;
    CCDyn symbols;                /* CMKSymbol[] */
} CMKScope;

struct cmk_function_info {
    CMKString name;
    CMKType return_type;
    CMKType *params;
    CCUINT32 param_count;
    CMKASTNodeId decl;
    CMKSemanticModule *module;
};

struct cmk_target_info {
    CMKString name;
    CMKASTNodeId decl;
    CMKSemanticModule *module;
    CCBOOL conditional;
};

typedef struct cmk_imported_function {
    CMKString name;               /* 当前模块中的本地名/别名 */
    const CMKFunctionInfo *function;
} CMKImportedFunction;

typedef struct cmk_imported_target {
    CMKString alias;              /* 模块别名 */
    const CMKTargetInfo *target;
} CMKImportedTarget;

typedef struct cmk_node_semantics {
    CMKType type;
    const CMKTargetInfo *target_ref;
    const CMKFunctionInfo *function;
    const CMKBuiltinSpec *builtin;
    CCBOOL noreturn;
} CMKNodeSemantics;

struct cmk_semantic_module {
    CMKAST *ast;
    char *path;
    CMKConfig *config;            /* 借用 */
    CMKBuiltinRegistry *builtins; /* 借用 */
    CMKDiagnostic *diagnostic;    /* 借用 */

    CMKScope *module_scope;
    CCDyn functions;              /* CMKFunctionInfo* */
    CCDyn targets;                /* CMKTargetInfo* */
    CCDyn imported_functions;     /* CMKImportedFunction* */
    CCDyn imported_targets;       /* CMKImportedTarget* */
    CMKNodeSemantics *nodes;      /* 按 AST node ID 索引 */
    CCUINT32 node_count;
    CCBOOL analyzed;
    CCBOOL ok;
};

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
