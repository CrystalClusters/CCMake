/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 内建函数注册表
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_builtin.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdarg.h>
#include <string.h>

#define CMK_BUILTIN_COMMENT CC_DBG_REL_TAG(".cmk 内建", NULL)

struct cmk_builtin_registry {
    CCDyn specs;   /* CMKBuiltinSpec*[]，注册表拥有副本 */
};

CMKBuiltinRegistry *cmk_builtin_registry_create(void)
{
    CMKBuiltinRegistry *registry =
        ccalloc(NULL, sizeof(CMKBuiltinRegistry), CMK_BUILTIN_COMMENT);
    if (!registry)
        return NULL;
    memset(registry, 0, sizeof(*registry));
    return registry;
}

void cmk_builtin_registry_destroy(CMKBuiltinRegistry *registry)
{
    if (!registry)
        return;
    CMKBuiltinSpec **specs = (CMKBuiltinSpec **)registry->specs.array;
    CCUINT32 count =
        registry->specs.size / (CCUINT32)sizeof(CMKBuiltinSpec *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (specs[i])
            ccalloc(specs[i], 0, NULL);
    }
    if (registry->specs.array)
        ccalloc(registry->specs.array, 0, NULL);
    ccalloc(registry, 0, NULL);
}

const CMKBuiltinSpec *cmk_builtin_find(
    const CMKBuiltinRegistry *registry, const char *name, CCUINT32 len
)
{
    if (!registry || !name || len == 0 || !registry->specs.array)
        return NULL;

    const CMKBuiltinSpec **specs =
        (const CMKBuiltinSpec **)registry->specs.array;
    CCUINT32 count =
        registry->specs.size / (CCUINT32)sizeof(const CMKBuiltinSpec *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKBuiltinSpec *spec = specs[i];
        if (!spec || !spec->name)
            continue;
        if (strlen(spec->name) == len &&
            memcmp(spec->name, name, len) == 0)
            return spec;
    }
    return NULL;
}

CCBOOL cmk_builtin_register(
    CMKBuiltinRegistry *registry, const CMKBuiltinSpec *spec
)
{
    if (!registry || !spec)
        return CCFALSE;
    if (spec->abi_version != CMK_BUILTIN_ABI_VERSION)
        return CCFALSE;
    if (!spec->name || spec->name[0] == '\0' || !spec->fn)
        return CCFALSE;
    if (spec->min_args > spec->max_args)
        return CCFALSE;
    if (spec->max_args > 0 && !spec->args)
        return CCFALSE;
    if (cmk_builtin_find(
            registry, spec->name, (CCUINT32)strlen(spec->name)
        ))
        return CCFALSE;

    CMKBuiltinSpec *owned =
        ccalloc(NULL, sizeof(CMKBuiltinSpec), CMK_BUILTIN_COMMENT);
    if (!owned)
        return CCFALSE;
    *owned = *spec;
    if (cc_dyn_core_push(
            &registry->specs,
            (CCUINT32)sizeof(CMKBuiltinSpec *),
            (CCINT32)sizeof(CMKBuiltinSpec *),
            &owned
        ) != 0)
    {
        ccalloc(owned, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL cmk_builtin_error(CMKBuiltinCall *call, const char *fmt, ...)
{
    if (!call || !call->diagnostic || !fmt)
        return CCFALSE;

    va_list ap;
    va_start(ap, fmt);
    (void)cmk_diagnostic_vset(
        call->diagnostic, CMK_DIAG_ERROR,
        call->file, call->line, call->col, fmt, ap
    );
    va_end(ap);
    return CCFALSE;
}

CCBOOL cmk_builtin_arg_matches(
    const CMKType *formal, const CMKType *actual
)
{
    if (!formal || !actual || formal->kind != actual->kind)
        return CCFALSE;
    if (formal->kind != CMK_VALUE_LIST)
        return CCTRUE;
    if (formal->element == CMK_VALUE_VOID)
        return CCTRUE;
    return (formal->element == actual->element) ? CCTRUE : CCFALSE;
}
