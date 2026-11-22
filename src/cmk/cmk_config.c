/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 语义/运行时访问 .ccm 配置树的适配层
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_config.h"

#include "ccvar.h"

#include <string.h>

typedef struct cmk_config_scan {
    CMKValueType element;
    CCBOOL has_element;
    CCBOOL mixed;
    CCBOOL nested;
} CMKConfigScan;

void cmk_config_init(CMKConfig *config, CCVarTree *tree)
{
    if (!config)
        return;
    config->tree = tree;
}

static CCBOOL config_scalar_type(
    CCVarType type, CMKValueType *out
)
{
    switch (type)
    {
    case CC_VAR_STRING:
        *out = CMK_VALUE_STRING;
        return CCTRUE;

    case CC_VAR_INT:
        *out = CMK_VALUE_INT;
        return CCTRUE;

    case CC_VAR_FLOAT:
        *out = CMK_VALUE_FLOAT;
        return CCTRUE;

    case CC_VAR_BOOL:
        *out = CMK_VALUE_BOOL;
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
    CMKConfigScan *scan = (CMKConfigScan *)user;
    CMKValueType type;
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

CMKConfigResult cmk_config_type(
    const CMKConfig *config, const char *path, CMKType *out
)
{
    if (!config || !config->tree || !path || !out)
        return CMK_CONFIG_TYPE;

    CCVarValue value;
    memset(&value, 0, sizeof(value));
    if (ccvar_get(config->tree, path, &value) != CC_VAR_OK)
        return CMK_CONFIG_MISSING;

    CMKValueType scalar;
    if (config_scalar_type(value.type, &scalar))
    {
        *out = cmk_type_scalar(scalar);
        return CMK_CONFIG_OK;
    }
    if (value.type != CC_VAR_ARRAY)
        return CMK_CONFIG_TYPE;

    CMKConfigScan scan;
    memset(&scan, 0, sizeof(scan));
    CCUINT32 count = ccvar_array_size(config->tree, path);
    if (count > 0)
    {
        (void)ccvar_foreach_element(
            config->tree, path, config_type_element, &scan
        );
    }
    if (scan.mixed)
        return CMK_CONFIG_MIXED;
    if (scan.nested)
        return CMK_CONFIG_TYPE;

    *out = cmk_type_list(
        scan.has_element ? scan.element : CMK_VALUE_VOID
    );
    return CMK_CONFIG_OK;
}

typedef struct cmk_config_value_build {
    CMKList *list;
    CMKValueType expected;
    CCBOOL mixed;
    CCBOOL nested;
    CCBOOL failed;
} CMKConfigValueBuild;

static CCINT32 config_value_element(
    CCUINT32 index, const CCVarValue *value, void *user
)
{
    (void)index;
    CMKConfigValueBuild *build = (CMKConfigValueBuild *)user;
    CMKValueType type;
    if (!config_scalar_type(value->type, &type))
    {
        build->nested = CCTRUE;
        return 1;
    }
    if (build->list->element_type == CMK_VALUE_VOID)
        build->list->element_type = type;
    else if (build->list->element_type != type)
    {
        build->mixed = CCTRUE;
        return 1;
    }
    if (build->expected != CMK_VALUE_VOID &&
        build->expected != type)
    {
        build->mixed = CCTRUE;
        return 1;
    }

    CMKValue item;
    cmk_value_init(&item);
    CCBOOL ok = CCFALSE;
    switch (type)
    {
    case CMK_VALUE_STRING:
        ok = cmk_value_set_string(
            &item, value->as.string.data, value->as.string.len
        );
        break;

    case CMK_VALUE_INT:
        ok = cmk_value_set_int(&item, value->as.integer);
        break;

    case CMK_VALUE_FLOAT:
        ok = cmk_value_set_float(&item, value->as.floating);
        break;

    case CMK_VALUE_BOOL:
        ok = cmk_value_set_bool(&item, value->as.boolean);
        break;

    default:
        break;
    }
    if (!ok || !cmk_list_append(build->list, &item))
    {
        cmk_value_clear(&item);
        build->failed = CCTRUE;
        return 1;
    }
    cmk_value_clear(&item);
    return 0;
}

CMKConfigResult cmk_config_value(
    const CMKConfig *config, const char *path,
    CMKValueType expected_element, CMKValue *out
)
{
    if (!config || !config->tree || !path || !out)
        return CMK_CONFIG_TYPE;

    CCVarValue value;
    memset(&value, 0, sizeof(value));
    if (ccvar_get(config->tree, path, &value) != CC_VAR_OK)
        return CMK_CONFIG_MISSING;

    CMKValueType scalar;
    if (config_scalar_type(value.type, &scalar))
    {
        CCBOOL ok = CCFALSE;
        switch (scalar)
        {
        case CMK_VALUE_STRING:
            ok = cmk_value_set_string(
                out, value.as.string.data, value.as.string.len
            );
            break;

        case CMK_VALUE_INT:
            ok = cmk_value_set_int(out, value.as.integer);
            break;

        case CMK_VALUE_FLOAT:
            ok = cmk_value_set_float(out, value.as.floating);
            break;

        case CMK_VALUE_BOOL:
            ok = cmk_value_set_bool(out, value.as.boolean);
            break;

        default:
            break;
        }
        return ok ? CMK_CONFIG_OK : CMK_CONFIG_NOMEM;
    }
    if (value.type != CC_VAR_ARRAY)
        return CMK_CONFIG_TYPE;

    CMKList *list = cmk_list_create(expected_element);
    if (!list)
        return CMK_CONFIG_NOMEM;

    CMKConfigValueBuild build;
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
        cmk_list_destroy(list);
        if (build.failed)
            return CMK_CONFIG_NOMEM;
        return build.mixed ? CMK_CONFIG_MIXED : CMK_CONFIG_TYPE;
    }
    if (expected_element != CMK_VALUE_VOID &&
        list->element_type != expected_element)
    {
        cmk_list_destroy(list);
        return CMK_CONFIG_TYPE;
    }

    cmk_value_clear(out);
    out->type = CMK_VALUE_LIST;
    out->as.list = list;
    return CMK_CONFIG_OK;
}
