/*
 * @Description: MK 语义/运行时测试辅助。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_TEST_CONTEXT_H_
#define _INCLUDE_MK_TEST_CONTEXT_H_

#include "test_harness.h"

#include "ccvar.h"
#include "mk/mk_builtin.h"
#include "mk/mk_config.h"
#include "mk/mk_parser.h"
#include "mk/mk_runtime.h"
#include "mk/mk_semantic.h"

typedef struct mk_test_context {
    CCVarTree *tree;
    MKBuiltinRegistry *builtins;
    MKDiagnostic diagnostic;
    MKLexer lexer;
    MKParser parser;
    MKAST *ast;
    MKConfig config;
    MKSemanticModule *semantic;
    MKRuntime *runtime;
} MKTestContext;

void mk_test_context_init(MKTestContext *ctx);
CCBOOL mk_test_context_load(MKTestContext *ctx, const char *source);
CCBOOL mk_test_context_configure(MKTestContext *ctx);
void mk_test_context_clear(MKTestContext *ctx);

const MKDiagnostic *mk_test_context_diag(const MKTestContext *ctx);

CCBOOL mk_test_set_string(
    CCVarTree *tree, const char *path, const char *value
);
CCBOOL mk_test_set_int(CCVarTree *tree, const char *path, CCINT64 value);
CCBOOL mk_test_set_bool(CCVarTree *tree, const char *path, CCBOOL value);
CCBOOL mk_test_set_string_array(
    CCVarTree *tree, const char *path,
    const char *const *values, CCUINT32 count
);

#endif
