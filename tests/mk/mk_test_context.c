/*
 * @Description: MK 语义/运行时测试辅助。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_test_context.h"

#include <string.h>

void mk_test_context_init(MKTestContext *ctx)
{
    if (!ctx)
        return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->tree = ccvar_create();
    ctx->builtins = mk_builtin_registry_create();
    mk_diagnostic_init(&ctx->diagnostic);
    if (ctx->tree)
        mk_config_init(&ctx->config, ctx->tree);
    if (ctx->builtins)
        (void)mk_builtin_register_standard(ctx->builtins);
}

CCBOOL mk_test_context_load(MKTestContext *ctx, const char *source)
{
    if (!ctx || !source || !ctx->tree || !ctx->builtins)
        return CCFALSE;
    if (!mk_lexer_init(
            &ctx->lexer, "test.mk", source, (CCUINT64)strlen(source)
        ))
        return CCFALSE;
    if (!mk_parser_init(&ctx->parser, &ctx->lexer, NULL))
        return CCFALSE;
    if (!mk_parser_parse(&ctx->parser, &ctx->ast))
        return CCFALSE;

    ctx->semantic = mk_semantic_module_create(
        ctx->ast, "test.mk", &ctx->config,
        ctx->builtins, &ctx->diagnostic
    );
    if (!ctx->semantic)
        return CCFALSE;
    return mk_semantic_analyze(ctx->semantic);
}

CCBOOL mk_test_context_configure(MKTestContext *ctx)
{
    if (!ctx || !ctx->semantic)
        return CCFALSE;
    if (!ctx->runtime)
    {
        ctx->runtime = mk_runtime_create(
            ctx->semantic, &ctx->config,
            ctx->builtins, &ctx->diagnostic
        );
        if (!ctx->runtime)
            return CCFALSE;
    }
    return mk_runtime_configure(ctx->runtime);
}

const MKDiagnostic *mk_test_context_diag(const MKTestContext *ctx)
{
    return ctx ? &ctx->diagnostic : NULL;
}

void mk_test_context_clear(MKTestContext *ctx)
{
    if (!ctx)
        return;
    if (ctx->runtime)
        mk_runtime_destroy(ctx->runtime);
    if (ctx->semantic)
        mk_semantic_module_destroy(ctx->semantic);
    if (ctx->ast)
        mk_ast_destroy(ctx->ast);
    mk_parser_clear(&ctx->parser);
    mk_lexer_clear(&ctx->lexer);
    if (ctx->builtins)
        mk_builtin_registry_destroy(ctx->builtins);
    if (ctx->tree)
        ccvar_destroy(ctx->tree);
    mk_diagnostic_clear(&ctx->diagnostic);
    memset(ctx, 0, sizeof(*ctx));
}

CCBOOL mk_test_set_string(
    CCVarTree *tree, const char *path, const char *value
)
{
    return (ccvar_set(
        tree, path, CC_VAR_STRING, value,
        (CCUINT32)strlen(value), CC_VAR_WRITE_REPLACE
    ) == CC_VAR_OK) ? CCTRUE : CCFALSE;
}

CCBOOL mk_test_set_int(CCVarTree *tree, const char *path, CCINT64 value)
{
    return (ccvar_set(
        tree, path, CC_VAR_INT, &value, 0, CC_VAR_WRITE_REPLACE
    ) == CC_VAR_OK) ? CCTRUE : CCFALSE;
}

CCBOOL mk_test_set_bool(CCVarTree *tree, const char *path, CCBOOL value)
{
    return (ccvar_set(
        tree, path, CC_VAR_BOOL, &value, 0, CC_VAR_WRITE_REPLACE
    ) == CC_VAR_OK) ? CCTRUE : CCFALSE;
}

CCBOOL mk_test_set_string_array(
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
