/*
 * @Description: MK Semantic 内部布局；仅 Semantic/Runtime/Program 等内部模块使用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_SEMANTIC_INTERNAL_H_
#define _INCLUDE_MK_SEMANTIC_INTERNAL_H_

#include "mk_semantic.h"

typedef enum mk_symbol_kind {
    MK_SYMBOL_VAR = 0,
    MK_SYMBOL_FUNCTION
} MKSymbolKind;

typedef struct mk_symbol {
    MKString name;
    MKSymbolKind kind;
    MKType type;                 /* 变量类型或函数返回类型 */
    CCBOOL is_const;
    const MKFunctionInfo *function;
} MKSymbol;

typedef struct mk_scope {
    struct mk_scope *parent;
    CCDyn symbols;                /* MKSymbol[] */
} MKScope;

struct mk_function_info {
    MKString name;
    MKType return_type;
    MKType *params;
    CCUINT32 param_count;
    MKASTNodeId decl;
    MKSemanticModule *module;
};


typedef struct mk_imported_function {
    MKString name;               /* 当前模块中的本地名/别名 */
    const MKFunctionInfo *function;
} MKImportedFunction;


typedef struct mk_node_semantics {
    MKType type;
    const MKFunctionInfo *function;
    const MKBuiltinSpec *builtin;
    CCBOOL noreturn;
} MKNodeSemantics;

struct mk_semantic_module {
    MKAST *ast;
    char *path;
    MKConfig *config;            /* 借用 */
    MKBuiltinRegistry *builtins; /* 借用 */
    MKDiagnostic *diagnostic;    /* 借用 */

    MKScope *module_scope;
    CCDyn functions;              /* MKFunctionInfo* */
    CCDyn imported_functions;     /* MKImportedFunction* */
    MKNodeSemantics *nodes;      /* 按 AST node ID 索引 */
    CCUINT32 node_count;
    CCBOOL analyzed;
    CCBOOL ok;
};

#endif
