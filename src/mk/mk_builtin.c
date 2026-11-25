/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 内建函数注册表
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_builtin.h"
#include "mk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdarg.h>
#include <string.h>

#define MK_BUILTIN_COMMENT CC_DBG_REL_TAG(".mk 内建", NULL)

struct mk_builtin_registry {
    CCDyn specs;   /* MKBuiltinSpec*[]，注册表拥有副本 */
};

MKBuiltinRegistry *mk_builtin_registry_create(void)
{
    MKBuiltinRegistry *registry =
        ccalloc(NULL, sizeof(MKBuiltinRegistry), MK_BUILTIN_COMMENT);
    if (!registry)
        return NULL;
    memset(registry, 0, sizeof(*registry));
    return registry;
}

void mk_builtin_registry_destroy(MKBuiltinRegistry *registry)
{
    if (!registry)
        return;
    MKBuiltinSpec **specs = (MKBuiltinSpec **)registry->specs.array;
    CCUINT32 count =
        registry->specs.size / (CCUINT32)sizeof(MKBuiltinSpec *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (specs[i])
            ccalloc(specs[i], 0, NULL);
    }
    if (registry->specs.array)
        ccalloc(registry->specs.array, 0, NULL);
    ccalloc(registry, 0, NULL);
}

const MKBuiltinSpec *mk_builtin_find(
    const MKBuiltinRegistry *registry, const char *name, CCUINT32 len
)
{
    if (!registry || !name || len == 0 || !registry->specs.array)
        return NULL;

    const MKBuiltinSpec **specs =
        (const MKBuiltinSpec **)registry->specs.array;
    CCUINT32 count =
        registry->specs.size / (CCUINT32)sizeof(const MKBuiltinSpec *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const MKBuiltinSpec *spec = specs[i];
        if (!spec || !spec->name)
            continue;
        if (strlen(spec->name) == len &&
            memcmp(spec->name, name, len) == 0)
            return spec;
    }
    return NULL;
}

CCBOOL mk_builtin_register(
    MKBuiltinRegistry *registry, const MKBuiltinSpec *spec
)
{
    if (!registry || !spec)
        return CCFALSE;
    if (spec->abi_version != MK_BUILTIN_ABI_VERSION)
        return CCFALSE;
    if (!spec->name || spec->name[0] == '\0' || !spec->fn)
        return CCFALSE;
    if (spec->min_args > spec->max_args)
        return CCFALSE;
    if (spec->max_args > 0 && !spec->args)
        return CCFALSE;
    if (mk_builtin_find(
            registry, spec->name, (CCUINT32)strlen(spec->name)
        ))
        return CCFALSE;

    MKBuiltinSpec *owned =
        ccalloc(NULL, sizeof(MKBuiltinSpec), MK_BUILTIN_COMMENT);
    if (!owned)
        return CCFALSE;
    *owned = *spec;
    if (!mk_dyn_push(
            &registry->specs,
            (CCUINT32)sizeof(MKBuiltinSpec *),
            (CCINT32)sizeof(MKBuiltinSpec *),
            &owned
        ))
    {
        ccalloc(owned, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL mk_builtin_error(MKBuiltinCall *call, const char *fmt, ...)
{
    if (!call || !call->diagnostic || !fmt)
        return CCFALSE;

    va_list ap;
    va_start(ap, fmt);
    (void)mk_diagnostic_vset(
        call->diagnostic, MK_DIAG_ERROR,
        call->file, call->line, call->col, fmt, ap
    );
    va_end(ap);
    return CCFALSE;
}

CCBOOL mk_builtin_arg_matches(
    const MKType *formal, const MKType *actual
)
{
    if (!formal || !actual || formal->kind != actual->kind)
        return CCFALSE;
    if (formal->kind != MK_VALUE_LIST)
        return CCTRUE;
    if (formal->element == MK_VALUE_VOID)
        return CCTRUE;
    return (formal->element == actual->element) ? CCTRUE : CCFALSE;
}
