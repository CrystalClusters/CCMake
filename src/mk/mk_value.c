/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 值模型、类型、列表、动作与目标引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_value.h"
#include "mk_safe.h"

#include <ccalloc.h>

#include <stdio.h>
#include <string.h>

#define MK_VALUE_COMMENT CC_DBG_REL_TAG(".mk 值", NULL)

/* ============================================================================
 * 类型与字符串
 * ========================================================================== */

MKType mk_type_scalar(MKValueType kind)
{
    MKType type;
    memset(&type, 0, sizeof(type));
    type.kind = kind;
    type.element = MK_VALUE_VOID;
    return type;
}

MKType mk_type_list(MKValueType element)
{
    MKType type;
    memset(&type, 0, sizeof(type));
    type.kind = MK_VALUE_LIST;
    type.element = element;
    return type;
}

CCBOOL mk_type_equal(const MKType *a, const MKType *b)
{
    if (!a || !b)
        return CCFALSE;
    if (a->kind != b->kind)
        return CCFALSE;
    if (a->kind == MK_VALUE_LIST)
        return (a->element == b->element) ? CCTRUE : CCFALSE;
    return CCTRUE;
}

CCBOOL mk_type_is_numeric(const MKType *type)
{
    if (!type)
        return CCFALSE;
    return (type->kind == MK_VALUE_INT || type->kind == MK_VALUE_FLOAT)
        ? CCTRUE : CCFALSE;
}

CCBOOL mk_type_is_scalar(const MKType *type)
{
    if (!type)
        return CCFALSE;
    switch (type->kind)
    {
    case MK_VALUE_INT:
    case MK_VALUE_FLOAT:
    case MK_VALUE_BOOL:
    case MK_VALUE_STRING:
    default:
        return CCFALSE;
    }
}

const char *mk_value_type_name(MKValueType type)
{
    switch (type)
    {
    case MK_VALUE_VOID:
        return "void";

    case MK_VALUE_INT:
        return "int";

    case MK_VALUE_FLOAT:
        return "float";

    case MK_VALUE_BOOL:
        return "bool";

    case MK_VALUE_STRING:
        return "string";

    case MK_VALUE_LIST:
        return "list";

    default:
        return "未知类型";
    }
}

CCBOOL mk_type_format(
    const MKType *type, char *buf, CCUINT32 size
)
{
    if (!type || !buf || size == 0)
        return CCFALSE;

    if (type->kind == MK_VALUE_LIST)
    {
        int n = snprintf(
            buf, size, "list<%s>",
            mk_value_type_name(type->element)
        );
        return (n > 0 && (CCUINT32)n < size) ? CCTRUE : CCFALSE;
    }

    int n = snprintf(buf, size, "%s", mk_value_type_name(type->kind));
    return (n > 0 && (CCUINT32)n < size) ? CCTRUE : CCFALSE;
}

void mk_string_init(MKString *string)
{
    if (!string)
        return;
    memset(string, 0, sizeof(*string));
}

CCBOOL mk_string_set(
    MKString *string, const char *data, CCUINT32 len
)
{
    if (!string || (!data && len != 0))
        return CCFALSE;

    char *copy = ccalloc(
        NULL, (CCUINT64)len + 1, MK_VALUE_COMMENT
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

void mk_string_clear(MKString *string)
{
    if (!string)
        return;
    if (string->data)
        ccalloc(string->data, 0, NULL);
    memset(string, 0, sizeof(*string));
}

CCBOOL mk_string_equal(const MKString *a, const MKString *b)
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

CCBOOL mk_string_append(
    MKString *string, const char *data, CCUINT32 len
)
{
    if (!string || (!data && len != 0))
        return CCFALSE;
    if (len == 0)
        return CCTRUE;

    char *grown = ccalloc(
        NULL, (CCUINT64)string->len + (CCUINT64)len + 1,
        MK_VALUE_COMMENT
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

MKList *mk_list_create(MKValueType element_type)
{
    MKList *list = ccalloc(NULL, sizeof(MKList), MK_VALUE_COMMENT);
    if (!list)
        return NULL;
    memset(list, 0, sizeof(*list));
    list->element_type = element_type;
    return list;
}

static void mk_list_clear(MKList *list)
{
    if (!list)
        return;
    MKValue *items = (MKValue *)list->elements.array;
    CCUINT32 count =
        list->elements.size / (CCUINT32)sizeof(MKValue);
    for (CCUINT32 i = 0; i < count; i++)
        mk_value_clear(&items[i]);
    if (list->elements.array)
        ccalloc(list->elements.array, 0, NULL);
    memset(&list->elements, 0, sizeof(list->elements));
}

void mk_list_destroy(MKList *list)
{
    if (!list)
        return;
    mk_list_clear(list);
    ccalloc(list, 0, NULL);
}

CCUINT32 mk_list_count(const MKList *list)
{
    if (!list)
        return 0;
    return list->elements.size / (CCUINT32)sizeof(MKValue);
}

const MKValue *mk_list_at(const MKList *list, CCUINT32 index)
{
    if (!list || index >= mk_list_count(list))
        return NULL;
    return &((const MKValue *)list->elements.array)[index];
}

CCBOOL mk_list_append(MKList *list, const MKValue *value)
{
    if (!list || !value)
        return CCFALSE;

    MKValue copy;
    mk_value_init(&copy);
    if (!mk_value_copy(&copy, value))
        return CCFALSE;
    if (!mk_dyn_push(
            &list->elements,
            (CCUINT32)sizeof(MKValue),
            (CCINT32)sizeof(MKValue),
            &copy
        ))
    {
        mk_value_clear(&copy);
        return CCFALSE;
    }
    return CCTRUE;
}

/* ============================================================================
 * 值
 * ========================================================================== */

void mk_value_init(MKValue *value)
{
    if (value)
        memset(value, 0, sizeof(*value));
}


void mk_value_clear(MKValue *value)
{
    if (!value)
        return;

    switch (value->type)
    {
    case MK_VALUE_STRING:
        mk_string_clear(&value->as.string);
        break;

    case MK_VALUE_LIST:
        if (value->as.list)
            mk_list_destroy(value->as.list);
        break;

    default:
        break;
    }
    memset(value, 0, sizeof(*value));
}

static MKList *mk_list_copy(const MKList *source)
{
    if (!source)
        return NULL;
    MKList *copy = mk_list_create(source->element_type);
    if (!copy)
        return NULL;

    CCUINT32 count = mk_list_count(source);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const MKValue *item = mk_list_at(source, i);
        if (!item || !mk_list_append(copy, item))
        {
            mk_list_destroy(copy);
            return NULL;
        }
    }
    return copy;
}


CCBOOL mk_value_copy(MKValue *dst, const MKValue *src)
{
    if (!dst || !src)
        return CCFALSE;
    if (dst == src)
        return CCTRUE;

    mk_value_clear(dst);
    dst->type = src->type;

    switch (src->type)
    {
    case MK_VALUE_VOID:
        return CCTRUE;

    case MK_VALUE_INT:
        dst->as.integer = src->as.integer;
        return CCTRUE;

    case MK_VALUE_FLOAT:
        dst->as.floating = src->as.floating;
        return CCTRUE;

    case MK_VALUE_BOOL:
        dst->as.boolean = src->as.boolean;
        return CCTRUE;

    case MK_VALUE_STRING:
        if (!mk_string_set(
                &dst->as.string,
                src->as.string.data, src->as.string.len
            ))
            goto Failed;
        return CCTRUE;

    case MK_VALUE_LIST:
        dst->as.list = mk_list_copy(src->as.list);
        return dst->as.list ? CCTRUE : CCFALSE;

    default:
        break;
    }

Failed:
    mk_value_clear(dst);
    return CCFALSE;
}

CCBOOL mk_value_set_void(MKValue *value)
{
    if (!value)
        return CCFALSE;
    mk_value_clear(value);
    return CCTRUE;
}

CCBOOL mk_value_set_int(MKValue *value, CCINT64 integer)
{
    if (!value)
        return CCFALSE;
    mk_value_clear(value);
    value->type = MK_VALUE_INT;
    value->as.integer = integer;
    return CCTRUE;
}

CCBOOL mk_value_set_float(MKValue *value, double floating)
{
    if (!value)
        return CCFALSE;
    mk_value_clear(value);
    value->type = MK_VALUE_FLOAT;
    value->as.floating = floating;
    return CCTRUE;
}

CCBOOL mk_value_set_bool(MKValue *value, CCBOOL boolean)
{
    if (!value)
        return CCFALSE;
    mk_value_clear(value);
    value->type = MK_VALUE_BOOL;
    value->as.boolean = boolean;
    return CCTRUE;
}

CCBOOL mk_value_set_string(
    MKValue *value, const char *data, CCUINT32 len
)
{
    if (!value)
        return CCFALSE;
    mk_value_clear(value);
    if (!mk_string_set(&value->as.string, data, len))
        return CCFALSE;
    value->type = MK_VALUE_STRING;
    return CCTRUE;
}

CCBOOL mk_value_set_list(MKValue *value, MKValueType element_type)
{
    if (!value)
        return CCFALSE;
    MKList *list = mk_list_create(element_type);
    if (!list)
        return CCFALSE;
    mk_value_clear(value);
    value->type = MK_VALUE_LIST;
    value->as.list = list;
    return CCTRUE;
}


CCBOOL mk_value_as_bool(const MKValue *value, CCBOOL *out)
{
    if (!value || !out || value->type != MK_VALUE_BOOL)
        return CCFALSE;
    *out = value->as.boolean;
    return CCTRUE;
}

CCBOOL mk_value_as_int(const MKValue *value, CCINT64 *out)
{
    if (!value || !out || value->type != MK_VALUE_INT)
        return CCFALSE;
    *out = value->as.integer;
    return CCTRUE;
}

CCBOOL mk_value_as_float(const MKValue *value, double *out)
{
    if (!value || !out || value->type != MK_VALUE_FLOAT)
        return CCFALSE;
    *out = value->as.floating;
    return CCTRUE;
}

CCBOOL mk_value_as_string(const MKValue *value, MKString *out)
{
    if (!value || !out || value->type != MK_VALUE_STRING)
        return CCFALSE;
    return mk_string_set(
        out, value->as.string.data, value->as.string.len
    );
}

static CCBOOL value_append_text(
    MKString *out, const MKValue *value, CCBOOL nested
)
{
    switch (value->type)
    {
    case MK_VALUE_VOID:
        return mk_string_append(out, "void", 4);

    case MK_VALUE_INT:
    {
        char text[32];
        int n = snprintf(text, sizeof(text), "%lld", (long long)value->as.integer);
        return (n > 0)
            ? mk_string_append(out, text, (CCUINT32)n) : CCFALSE;
    }

    case MK_VALUE_FLOAT:
    {
        char text[64];
        int n = snprintf(text, sizeof(text), "%g", value->as.floating);
        return (n > 0)
            ? mk_string_append(out, text, (CCUINT32)n) : CCFALSE;
    }

    case MK_VALUE_BOOL:
        return value->as.boolean
            ? mk_string_append(out, "true", 4)
            : mk_string_append(out, "false", 5);

    case MK_VALUE_STRING:
        return mk_string_append(
            out, value->as.string.data, value->as.string.len
        );

    case MK_VALUE_LIST:
    {
        if (!mk_string_append(out, "[", 1))
            return CCFALSE;
        CCUINT32 count = mk_list_count(value->as.list);
        for (CCUINT32 i = 0; i < count; i++)
        {
            if (i > 0 && !mk_string_append(out, ", ", 2))
                return CCFALSE;
            const MKValue *item = mk_list_at(value->as.list, i);
            if (!item || !value_append_text(out, item, CCTRUE))
                return CCFALSE;
        }
        if (!mk_string_append(out, "]", 1))
            return CCFALSE;
        return CCTRUE;
    }

    default:
        break;
    }
    (void)nested;
    return CCFALSE;
}

CCBOOL mk_value_to_string(const MKValue *value, MKString *out)
{
    if (!value || !out)
        return CCFALSE;
    mk_string_clear(out);
    return value_append_text(out, value, CCFALSE);
}

CCBOOL mk_value_equal(const MKValue *a, const MKValue *b)
{
    if (!a || !b || a->type != b->type)
        return CCFALSE;

    switch (a->type)
    {
    case MK_VALUE_VOID:
        return CCTRUE;

    case MK_VALUE_INT:
        return (a->as.integer == b->as.integer) ? CCTRUE : CCFALSE;

    case MK_VALUE_FLOAT:
        return (a->as.floating == b->as.floating) ? CCTRUE : CCFALSE;

    case MK_VALUE_BOOL:
        return (a->as.boolean == b->as.boolean) ? CCTRUE : CCFALSE;

    case MK_VALUE_STRING:
        return mk_string_equal(&a->as.string, &b->as.string);

    case MK_VALUE_LIST:
    {
        if (!a->as.list || !b->as.list)
            return CCFALSE;
        if (a->as.list->element_type != b->as.list->element_type)
            return CCFALSE;
        CCUINT32 count = mk_list_count(a->as.list);
        if (count != mk_list_count(b->as.list))
            return CCFALSE;
        for (CCUINT32 i = 0; i < count; i++)
        {
            const MKValue *x = mk_list_at(a->as.list, i);
            const MKValue *y = mk_list_at(b->as.list, i);
            if (!x || !y || !mk_value_equal(x, y))
                return CCFALSE;
        }
        return CCTRUE;
    }

    default:
        break;
    }
    return CCFALSE;
}
