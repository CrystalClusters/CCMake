/*
 * @Description: CMK 语义/运行时测试辅助。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_test_context.h"

#include <string.h>

void cmk_test_context_init(CMKTestContext *ctx)
{
    if (!ctx)
        return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->tree = ccvar_create();
    ctx->builtins = cmk_builtin_registry_create();
    cmk_diagnostic_init(&ctx->diagnostic);
    if (ctx->tree)
        cmk_config_init(&ctx->config, ctx->tree);
    if (ctx->builtins)
        (void)cmk_builtin_register_standard(ctx->builtins);
}

CCBOOL cmk_test_context_load(CMKTestContext *ctx, const char *source)
{
    if (!ctx || !source || !ctx->tree || !ctx->builtins)
        return CCFALSE;
    if (!cmk_lexer_init(
            &ctx->lexer, "test.cmk", source, (CCUINT64)strlen(source)
        ))
        return CCFALSE;
    if (!cmk_parser_init(&ctx->parser, &ctx->lexer, NULL))
        return CCFALSE;
    if (!cmk_parser_parse(&ctx->parser, &ctx->ast))
        return CCFALSE;

    ctx->semantic = cmk_semantic_module_create(
        ctx->ast, "test.cmk", &ctx->config,
        ctx->builtins, &ctx->diagnostic
    );
    if (!ctx->semantic)
        return CCFALSE;
    return cmk_semantic_analyze(ctx->semantic);
}

CCBOOL cmk_test_context_configure(CMKTestContext *ctx)
{
    if (!ctx || !ctx->semantic)
        return CCFALSE;
    if (!ctx->runtime)
    {
        ctx->runtime = cmk_runtime_create(
            ctx->semantic, &ctx->config,
            ctx->builtins, &ctx->diagnostic
        );
        if (!ctx->runtime)
            return CCFALSE;
    }
    return cmk_runtime_configure(ctx->runtime);
}

const CMKDiagnostic *cmk_test_context_diag(const CMKTestContext *ctx)
{
    return ctx ? &ctx->diagnostic : NULL;
}

void cmk_test_context_clear(CMKTestContext *ctx)
{
    if (!ctx)
        return;
    if (ctx->runtime)
        cmk_runtime_destroy(ctx->runtime);
    if (ctx->semantic)
        cmk_semantic_module_destroy(ctx->semantic);
    if (ctx->ast)
        cmk_ast_destroy(ctx->ast);
    cmk_parser_clear(&ctx->parser);
    cmk_lexer_clear(&ctx->lexer);
    if (ctx->builtins)
        cmk_builtin_registry_destroy(ctx->builtins);
    if (ctx->tree)
        ccvar_destroy(ctx->tree);
    cmk_diagnostic_clear(&ctx->diagnostic);
    memset(ctx, 0, sizeof(*ctx));
}

CCBOOL cmk_test_set_string(
    CCVarTree *tree, const char *path, const char *value
)
{
    return (ccvar_set(
        tree, path, CC_VAR_STRING, value,
        (CCUINT32)strlen(value), CC_VAR_WRITE_REPLACE
    ) == CC_VAR_OK) ? CCTRUE : CCFALSE;
}

CCBOOL cmk_test_set_int(CCVarTree *tree, const char *path, CCINT64 value)
{
    return (ccvar_set(
        tree, path, CC_VAR_INT, &value, 0, CC_VAR_WRITE_REPLACE
    ) == CC_VAR_OK) ? CCTRUE : CCFALSE;
}

CCBOOL cmk_test_set_bool(CCVarTree *tree, const char *path, CCBOOL value)
{
    return (ccvar_set(
        tree, path, CC_VAR_BOOL, &value, 0, CC_VAR_WRITE_REPLACE
    ) == CC_VAR_OK) ? CCTRUE : CCFALSE;
}

CCBOOL cmk_test_set_string_array(
    CCVarTree *tree, const char *path,
    const char *const *values, CCUINT32 count
)
{
    if (ccvar_set(
            tree, path, CC_VAR_ARRAY, NULL, 0, CC_VAR_WRITE_REPLACE
        ) != CC_VAR_OK)
        return CCFALSE;
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (ccvar_array_push(
                tree, path, CC_VAR_STRING, values[i],
                (CCUINT32)strlen(values[i]), NULL
            ) != CC_VAR_OK)
            return CCFALSE;
    }
    return CCTRUE;
}
