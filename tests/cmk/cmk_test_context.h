/*
 * @Description: CMK 语义/运行时测试辅助。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_TEST_CONTEXT_H_
#define _INCLUDE_CMK_TEST_CONTEXT_H_

#include "test_harness.h"

#include "ccvar.h"
#include "cmk/cmk_builtin.h"
#include "cmk/cmk_config.h"
#include "cmk/cmk_parser.h"
#include "cmk/cmk_runtime.h"
#include "cmk/cmk_semantic.h"

typedef struct cmk_test_context {
    CCVarTree *tree;
    CMKBuiltinRegistry *builtins;
    CMKDiagnostic diagnostic;
    CMKLexer lexer;
    CMKParser parser;
    CMKAST *ast;
    CMKConfig config;
    CMKSemanticModule *semantic;
    CMKRuntime *runtime;
} CMKTestContext;

void cmk_test_context_init(CMKTestContext *ctx);
CCBOOL cmk_test_context_load(CMKTestContext *ctx, const char *source);
CCBOOL cmk_test_context_configure(CMKTestContext *ctx);
void cmk_test_context_clear(CMKTestContext *ctx);

const CMKDiagnostic *cmk_test_context_diag(const CMKTestContext *ctx);

CCBOOL cmk_test_set_string(
    CCVarTree *tree, const char *path, const char *value
);
CCBOOL cmk_test_set_int(CCVarTree *tree, const char *path, CCINT64 value);
CCBOOL cmk_test_set_bool(CCVarTree *tree, const char *path, CCBOOL value);
CCBOOL cmk_test_set_string_array(
    CCVarTree *tree, const char *path,
    const char *const *values, CCUINT32 count
);

#endif
