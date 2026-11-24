/*
 * @Description: CMK Semantic 内部布局；仅 Semantic/Runtime/Program 等内部模块使用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_SEMANTIC_INTERNAL_H_
#define _INCLUDE_CMK_SEMANTIC_INTERNAL_H_

#include "cmk_semantic.h"

typedef enum cmk_symbol_kind {
    CMK_SYMBOL_VAR = 0,
    CMK_SYMBOL_FUNCTION
} CMKSymbolKind;

typedef struct cmk_symbol {
    CMKString name;
    CMKSymbolKind kind;
    CMKType type;                 /* 变量类型或函数返回类型 */
    CCBOOL is_const;
    const CMKFunctionInfo *function;
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


typedef struct cmk_imported_function {
    CMKString name;               /* 当前模块中的本地名/别名 */
    const CMKFunctionInfo *function;
} CMKImportedFunction;


typedef struct cmk_node_semantics {
    CMKType type;
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
    CCDyn imported_functions;     /* CMKImportedFunction* */
    CMKNodeSemantics *nodes;      /* 按 AST node ID 索引 */
    CCUINT32 node_count;
    CCBOOL analyzed;
    CCBOOL ok;
};

#endif
