/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 值模型、类型、列表、动作与目标引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_value.h"
#include "cmk_safe.h"

#include <ccalloc.h>

#include <stdio.h>
#include <string.h>

#define CMK_VALUE_COMMENT CC_DBG_REL_TAG(".cmk 值", NULL)

/* ============================================================================
 * 类型与字符串
 * ========================================================================== */

CMKType cmk_type_scalar(CMKValueType kind)
{
    CMKType type;
    memset(&type, 0, sizeof(type));
    type.kind = kind;
    type.element = CMK_VALUE_VOID;
    return type;
}

CMKType cmk_type_list(CMKValueType element)
{
    CMKType type;
    memset(&type, 0, sizeof(type));
    type.kind = CMK_VALUE_LIST;
    type.element = element;
    return type;
}

CCBOOL cmk_type_equal(const CMKType *a, const CMKType *b)
{
    if (!a || !b)
        return CCFALSE;
    if (a->kind != b->kind)
        return CCFALSE;
    if (a->kind == CMK_VALUE_LIST)
        return (a->element == b->element) ? CCTRUE : CCFALSE;
    return CCTRUE;
}

CCBOOL cmk_type_is_numeric(const CMKType *type)
{
    if (!type)
        return CCFALSE;
    return (type->kind == CMK_VALUE_INT || type->kind == CMK_VALUE_FLOAT)
        ? CCTRUE : CCFALSE;
}

CCBOOL cmk_type_is_scalar(const CMKType *type)
{
    if (!type)
        return CCFALSE;
    switch (type->kind)
    {
    case CMK_VALUE_INT:
    case CMK_VALUE_FLOAT:
    case CMK_VALUE_BOOL:
    case CMK_VALUE_STRING:
    default:
        return CCFALSE;
    }
}

const char *cmk_value_type_name(CMKValueType type)
{
    switch (type)
    {
    case CMK_VALUE_VOID:
        return "void";

    case CMK_VALUE_INT:
        return "int";

    case CMK_VALUE_FLOAT:
        return "float";

    case CMK_VALUE_BOOL:
        return "bool";

    case CMK_VALUE_STRING:
        return "string";

    case CMK_VALUE_LIST:
        return "list";

    default:
        return "未知类型";
    }
}

CCBOOL cmk_type_format(
    const CMKType *type, char *buf, CCUINT32 size
)
{
    if (!type || !buf || size == 0)
        return CCFALSE;

    if (type->kind == CMK_VALUE_LIST)
    {
        int n = snprintf(
            buf, size, "list<%s>",
            cmk_value_type_name(type->element)
        );
        return (n > 0 && (CCUINT32)n < size) ? CCTRUE : CCFALSE;
    }

    int n = snprintf(buf, size, "%s", cmk_value_type_name(type->kind));
    return (n > 0 && (CCUINT32)n < size) ? CCTRUE : CCFALSE;
}

void cmk_string_init(CMKString *string)
{
    if (!string)
        return;
    memset(string, 0, sizeof(*string));
}

CCBOOL cmk_string_set(
    CMKString *string, const char *data, CCUINT32 len
)
{
    if (!string || (!data && len != 0))
        return CCFALSE;

    char *copy = ccalloc(
        NULL, (CCUINT64)len + 1, CMK_VALUE_COMMENT
    );
    if (!copy)
        return CCFALSE;
    if (len > 0)
        memcpy(copy, data, len);
    copy[len] = '\0';

    if (string->data)
        ccalloc(string->data, 0, NULL);
    string->data = copy;
    string->len = len;
    return CCTRUE;
}

void cmk_string_clear(CMKString *string)
{
    if (!string)
        return;
    if (string->data)
        ccalloc(string->data, 0, NULL);
    memset(string, 0, sizeof(*string));
}

CCBOOL cmk_string_equal(const CMKString *a, const CMKString *b)
{
    if (!a || !b)
        return CCFALSE;
    if (a->len != b->len)
        return CCFALSE;
    if (a->len == 0)
        return CCTRUE;
    if (!a->data || !b->data)
        return CCFALSE;
    return (memcmp(a->data, b->data, a->len) == 0)
        ? CCTRUE : CCFALSE;
}

CCBOOL cmk_string_append(
    CMKString *string, const char *data, CCUINT32 len
)
{
    if (!string || (!data && len != 0))
        return CCFALSE;
    if (len == 0)
        return CCTRUE;

    char *grown = ccalloc(
        NULL, (CCUINT64)string->len + (CCUINT64)len + 1,
        CMK_VALUE_COMMENT
    );
    if (!grown)
        return CCFALSE;
    if (string->len > 0)
        memcpy(grown, string->data, string->len);
    memcpy(grown + string->len, data, len);
    string->len += len;
    grown[string->len] = '\0';
    if (string->data)
        ccalloc(string->data, 0, NULL);
    string->data = grown;
    return CCTRUE;
}

/* ============================================================================
 * 列表
 * ========================================================================== */

CMKList *cmk_list_create(CMKValueType element_type)
{
    CMKList *list = ccalloc(NULL, sizeof(CMKList), CMK_VALUE_COMMENT);
    if (!list)
        return NULL;
    memset(list, 0, sizeof(*list));
    list->element_type = element_type;
    return list;
}

static void cmk_list_clear(CMKList *list)
{
    if (!list)
        return;
    CMKValue *items = (CMKValue *)list->elements.array;
    CCUINT32 count =
        list->elements.size / (CCUINT32)sizeof(CMKValue);
    for (CCUINT32 i = 0; i < count; i++)
        cmk_value_clear(&items[i]);
    if (list->elements.array)
        ccalloc(list->elements.array, 0, NULL);
    memset(&list->elements, 0, sizeof(list->elements));
}

void cmk_list_destroy(CMKList *list)
{
    if (!list)
        return;
    cmk_list_clear(list);
    ccalloc(list, 0, NULL);
}

CCUINT32 cmk_list_count(const CMKList *list)
{
    if (!list)
        return 0;
    return list->elements.size / (CCUINT32)sizeof(CMKValue);
}

const CMKValue *cmk_list_at(const CMKList *list, CCUINT32 index)
{
    if (!list || index >= cmk_list_count(list))
        return NULL;
    return &((const CMKValue *)list->elements.array)[index];
}

CCBOOL cmk_list_append(CMKList *list, const CMKValue *value)
{
    if (!list || !value)
        return CCFALSE;

    CMKValue copy;
    cmk_value_init(&copy);
    if (!cmk_value_copy(&copy, value))
        return CCFALSE;
    if (!cmk_dyn_push(
            &list->elements,
            (CCUINT32)sizeof(CMKValue),
            (CCINT32)sizeof(CMKValue),
            &copy
        ))
    {
        cmk_value_clear(&copy);
        return CCFALSE;
    }
    return CCTRUE;
}

/* ============================================================================
 * 值
 * ========================================================================== */

void cmk_value_init(CMKValue *value)
{
    if (value)
        memset(value, 0, sizeof(*value));
}


void cmk_value_clear(CMKValue *value)
{
    if (!value)
        return;

    switch (value->type)
    {
    case CMK_VALUE_STRING:
        cmk_string_clear(&value->as.string);
        break;

    case CMK_VALUE_LIST:
        if (value->as.list)
            cmk_list_destroy(value->as.list);
        break;

    default:
        break;
    }
    memset(value, 0, sizeof(*value));
}

static CMKList *cmk_list_copy(const CMKList *source)
{
    if (!source)
        return NULL;
    CMKList *copy = cmk_list_create(source->element_type);
    if (!copy)
        return NULL;

    CCUINT32 count = cmk_list_count(source);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKValue *item = cmk_list_at(source, i);
        if (!item || !cmk_list_append(copy, item))
        {
            cmk_list_destroy(copy);
            return NULL;
        }
    }
    return copy;
}


CCBOOL cmk_value_copy(CMKValue *dst, const CMKValue *src)
{
    if (!dst || !src)
        return CCFALSE;
    if (dst == src)
        return CCTRUE;

    cmk_value_clear(dst);
    dst->type = src->type;

    switch (src->type)
    {
    case CMK_VALUE_VOID:
        return CCTRUE;

    case CMK_VALUE_INT:
        dst->as.integer = src->as.integer;
        return CCTRUE;

    case CMK_VALUE_FLOAT:
        dst->as.floating = src->as.floating;
        return CCTRUE;

    case CMK_VALUE_BOOL:
        dst->as.boolean = src->as.boolean;
        return CCTRUE;

    case CMK_VALUE_STRING:
        if (!cmk_string_set(
                &dst->as.string,
                src->as.string.data, src->as.string.len
            ))
            goto Failed;
        return CCTRUE;

    case CMK_VALUE_LIST:
        dst->as.list = cmk_list_copy(src->as.list);
        return dst->as.list ? CCTRUE : CCFALSE;

    default:
        break;
    }

Failed:
    cmk_value_clear(dst);
    return CCFALSE;
}

CCBOOL cmk_value_set_void(CMKValue *value)
{
    if (!value)
        return CCFALSE;
    cmk_value_clear(value);
    return CCTRUE;
}

CCBOOL cmk_value_set_int(CMKValue *value, CCINT64 integer)
{
    if (!value)
        return CCFALSE;
    cmk_value_clear(value);
    value->type = CMK_VALUE_INT;
    value->as.integer = integer;
    return CCTRUE;
}

CCBOOL cmk_value_set_float(CMKValue *value, double floating)
{
    if (!value)
        return CCFALSE;
    cmk_value_clear(value);
    value->type = CMK_VALUE_FLOAT;
    value->as.floating = floating;
    return CCTRUE;
}

CCBOOL cmk_value_set_bool(CMKValue *value, CCBOOL boolean)
{
    if (!value)
        return CCFALSE;
    cmk_value_clear(value);
    value->type = CMK_VALUE_BOOL;
    value->as.boolean = boolean;
    return CCTRUE;
}

CCBOOL cmk_value_set_string(
    CMKValue *value, const char *data, CCUINT32 len
)
{
    if (!value)
        return CCFALSE;
    cmk_value_clear(value);
    if (!cmk_string_set(&value->as.string, data, len))
        return CCFALSE;
    value->type = CMK_VALUE_STRING;
    return CCTRUE;
}

CCBOOL cmk_value_set_list(CMKValue *value, CMKValueType element_type)
{
    if (!value)
        return CCFALSE;
    CMKList *list = cmk_list_create(element_type);
    if (!list)
        return CCFALSE;
    cmk_value_clear(value);
    value->type = CMK_VALUE_LIST;
    value->as.list = list;
    return CCTRUE;
}


CCBOOL cmk_value_as_bool(const CMKValue *value, CCBOOL *out)
{
    if (!value || !out || value->type != CMK_VALUE_BOOL)
        return CCFALSE;
    *out = value->as.boolean;
    return CCTRUE;
}

CCBOOL cmk_value_as_int(const CMKValue *value, CCINT64 *out)
{
    if (!value || !out || value->type != CMK_VALUE_INT)
        return CCFALSE;
    *out = value->as.integer;
    return CCTRUE;
}

CCBOOL cmk_value_as_float(const CMKValue *value, double *out)
{
    if (!value || !out || value->type != CMK_VALUE_FLOAT)
        return CCFALSE;
    *out = value->as.floating;
    return CCTRUE;
}

CCBOOL cmk_value_as_string(const CMKValue *value, CMKString *out)
{
    if (!value || !out || value->type != CMK_VALUE_STRING)
        return CCFALSE;
    return cmk_string_set(
        out, value->as.string.data, value->as.string.len
    );
}

static CCBOOL value_append_text(
    CMKString *out, const CMKValue *value, CCBOOL nested
)
{
    switch (value->type)
    {
    case CMK_VALUE_VOID:
        return cmk_string_append(out, "void", 4);

    case CMK_VALUE_INT:
    {
        char text[32];
        int n = snprintf(text, sizeof(text), "%lld", (long long)value->as.integer);
        return (n > 0)
            ? cmk_string_append(out, text, (CCUINT32)n) : CCFALSE;
    }

    case CMK_VALUE_FLOAT:
    {
        char text[64];
        int n = snprintf(text, sizeof(text), "%g", value->as.floating);
        return (n > 0)
            ? cmk_string_append(out, text, (CCUINT32)n) : CCFALSE;
    }

    case CMK_VALUE_BOOL:
        return value->as.boolean
            ? cmk_string_append(out, "true", 4)
            : cmk_string_append(out, "false", 5);

    case CMK_VALUE_STRING:
        return cmk_string_append(
            out, value->as.string.data, value->as.string.len
        );

    case CMK_VALUE_LIST:
    {
        if (!cmk_string_append(out, "[", 1))
            return CCFALSE;
        CCUINT32 count = cmk_list_count(value->as.list);
        for (CCUINT32 i = 0; i < count; i++)
        {
            if (i > 0 && !cmk_string_append(out, ", ", 2))
                return CCFALSE;
            const CMKValue *item = cmk_list_at(value->as.list, i);
            if (!item || !value_append_text(out, item, CCTRUE))
                return CCFALSE;
        }
        if (!cmk_string_append(out, "]", 1))
            return CCFALSE;
        return CCTRUE;
    }

    default:
        break;
    }
    (void)nested;
    return CCFALSE;
}

CCBOOL cmk_value_to_string(const CMKValue *value, CMKString *out)
{
    if (!value || !out)
        return CCFALSE;
    cmk_string_clear(out);
    return value_append_text(out, value, CCFALSE);
}

CCBOOL cmk_value_equal(const CMKValue *a, const CMKValue *b)
{
    if (!a || !b || a->type != b->type)
        return CCFALSE;

    switch (a->type)
    {
    case CMK_VALUE_VOID:
        return CCTRUE;

    case CMK_VALUE_INT:
        return (a->as.integer == b->as.integer) ? CCTRUE : CCFALSE;

    case CMK_VALUE_FLOAT:
        return (a->as.floating == b->as.floating) ? CCTRUE : CCFALSE;

    case CMK_VALUE_BOOL:
        return (a->as.boolean == b->as.boolean) ? CCTRUE : CCFALSE;

    case CMK_VALUE_STRING:
        return cmk_string_equal(&a->as.string, &b->as.string);

    case CMK_VALUE_LIST:
    {
        if (!a->as.list || !b->as.list)
            return CCFALSE;
        if (a->as.list->element_type != b->as.list->element_type)
            return CCFALSE;
        CCUINT32 count = cmk_list_count(a->as.list);
        if (count != cmk_list_count(b->as.list))
            return CCFALSE;
        for (CCUINT32 i = 0; i < count; i++)
        {
            const CMKValue *x = cmk_list_at(a->as.list, i);
            const CMKValue *y = cmk_list_at(b->as.list, i);
            if (!x || !y || !cmk_value_equal(x, y))
                return CCFALSE;
        }
        return CCTRUE;
    }

    default:
        break;
    }
    return CCFALSE;
}
