/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 语义/运行时访问 .mc 配置树的适配层
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_config.h"

#include "ccvar.h"

#include <string.h>

typedef struct mk_config_scan {
    MKValueType element;
    CCBOOL has_element;
    CCBOOL mixed;
    CCBOOL nested;
} MKConfigScan;

void mk_config_init(MKConfig *config, CCVarTree *tree)
{
    if (!config)
        return;
    config->tree = tree;
}

static CCBOOL config_scalar_type(
    CCVarType type, MKValueType *out
)
{
    switch (type)
    {
    case CC_VAR_STRING:
        *out = MK_VALUE_STRING;
        return CCTRUE;

    case CC_VAR_INT:
        *out = MK_VALUE_INT;
        return CCTRUE;

    case CC_VAR_FLOAT:
        *out = MK_VALUE_FLOAT;
        return CCTRUE;

    case CC_VAR_BOOL:
        *out = MK_VALUE_BOOL;
        return CCTRUE;

    default:
        return CCFALSE;
    }
}

static CCINT32 config_type_element(
    CCUINT32 index, const CCVarValue *value, void *user
)
{
    (void)index;
    MKConfigScan *scan = (MKConfigScan *)user;
    MKValueType type;
    if (!config_scalar_type(value->type, &type))
    {
        scan->nested = CCTRUE;
        return 1;
    }
    if (!scan->has_element)
    {
        scan->element = type;
        scan->has_element = CCTRUE;
        return 0;
    }
    if (scan->element != type)
    {
        scan->mixed = CCTRUE;
        return 1;
    }
    return 0;
}

MKConfigResult mk_config_type(
    const MKConfig *config, const char *path, MKType *out
)
{
    if (!config || !config->tree || !path || !out)
        return MK_CONFIG_TYPE;

    CCVarValue value;
    memset(&value, 0, sizeof(value));
    if (ccvar_get(config->tree, path, &value) != CC_VAR_OK)
        return MK_CONFIG_MISSING;

    MKValueType scalar;
    if (config_scalar_type(value.type, &scalar))
    {
        *out = mk_type_scalar(scalar);
        return MK_CONFIG_OK;
    }
    if (value.type != CC_VAR_ARRAY)
        return MK_CONFIG_TYPE;

    MKConfigScan scan;
    memset(&scan, 0, sizeof(scan));
    CCUINT32 count = ccvar_array_size(config->tree, path);
    if (count > 0)
    {
        (void)ccvar_foreach_element(
            config->tree, path, config_type_element, &scan
        );
    }
    if (scan.mixed)
        return MK_CONFIG_MIXED;
    if (scan.nested)
        return MK_CONFIG_TYPE;

    *out = mk_type_list(
        scan.has_element ? scan.element : MK_VALUE_VOID
    );
    return MK_CONFIG_OK;
}

typedef struct mk_config_value_build {
    MKList *list;
    MKValueType expected;
    CCBOOL mixed;
    CCBOOL nested;
    CCBOOL failed;
} MKConfigValueBuild;

static CCINT32 config_value_element(
    CCUINT32 index, const CCVarValue *value, void *user
)
{
    (void)index;
    MKConfigValueBuild *build = (MKConfigValueBuild *)user;
    MKValueType type;
    if (!config_scalar_type(value->type, &type))
    {
        build->nested = CCTRUE;
        return 1;
    }
    if (build->list->element_type == MK_VALUE_VOID)
        build->list->element_type = type;
    else if (build->list->element_type != type)
    {
        build->mixed = CCTRUE;
        return 1;
    }
    if (build->expected != MK_VALUE_VOID &&
        build->expected != type)
    {
        build->mixed = CCTRUE;
        return 1;
    }

    MKValue item;
    mk_value_init(&item);
    CCBOOL ok = CCFALSE;
    switch (type)
    {
    case MK_VALUE_STRING:
        ok = mk_value_set_string(
            &item, value->as.string.data, value->as.string.len
        );
        break;

    case MK_VALUE_INT:
        ok = mk_value_set_int(&item, value->as.integer);
        break;

    case MK_VALUE_FLOAT:
        ok = mk_value_set_float(&item, value->as.floating);
        break;

    case MK_VALUE_BOOL:
        ok = mk_value_set_bool(&item, value->as.boolean);
        break;

    default:
        break;
    }
    if (!ok || !mk_list_append(build->list, &item))
    {
        mk_value_clear(&item);
        build->failed = CCTRUE;
        return 1;
    }
    mk_value_clear(&item);
    return 0;
}

MKConfigResult mk_config_value(
    const MKConfig *config, const char *path,
    MKValueType expected_element, MKValue *out
)
{
    if (!config || !config->tree || !path || !out)
        return MK_CONFIG_TYPE;

    CCVarValue value;
    memset(&value, 0, sizeof(value));
    if (ccvar_get(config->tree, path, &value) != CC_VAR_OK)
        return MK_CONFIG_MISSING;

    MKValueType scalar;
    if (config_scalar_type(value.type, &scalar))
    {
        CCBOOL ok = CCFALSE;
        switch (scalar)
        {
        case MK_VALUE_STRING:
            ok = mk_value_set_string(
                out, value.as.string.data, value.as.string.len
            );
            break;

        case MK_VALUE_INT:
            ok = mk_value_set_int(out, value.as.integer);
            break;

        case MK_VALUE_FLOAT:
            ok = mk_value_set_float(out, value.as.floating);
            break;

        case MK_VALUE_BOOL:
            ok = mk_value_set_bool(out, value.as.boolean);
            break;

        default:
            break;
        }
        return ok ? MK_CONFIG_OK : MK_CONFIG_NOMEM;
    }
    if (value.type != CC_VAR_ARRAY)
        return MK_CONFIG_TYPE;

    MKList *list = mk_list_create(expected_element);
    if (!list)
        return MK_CONFIG_NOMEM;

    MKConfigValueBuild build;
    memset(&build, 0, sizeof(build));
    build.list = list;
    build.expected = expected_element;

    CCUINT32 count = ccvar_array_size(config->tree, path);
    if (count > 0)
    {
        (void)ccvar_foreach_element(
            config->tree, path, config_value_element, &build
        );
    }
    if (build.mixed || build.nested || build.failed)
    {
        mk_list_destroy(list);
        if (build.failed)
            return MK_CONFIG_NOMEM;
        return build.mixed ? MK_CONFIG_MIXED : MK_CONFIG_TYPE;
    }
    if (expected_element != MK_VALUE_VOID &&
        list->element_type != expected_element)
    {
        mk_list_destroy(list);
        return MK_CONFIG_TYPE;
    }

    mk_value_clear(out);
    out->type = MK_VALUE_LIST;
    out->as.list = list;
    return MK_CONFIG_OK;
}
