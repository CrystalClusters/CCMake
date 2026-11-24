/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 值模型、类型、列表、动作与目标引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_VALUE_H_
#define _INCLUDE_CMK_VALUE_H_

#include <cc_dyn.h>
#include <crystal_cluster.h>

/* 语言值类型；CMK_VALUE_VOID 也用于未解析的空列表元素类型 */
typedef enum cmk_value_type {
    CMK_VALUE_VOID = 0,
    CMK_VALUE_INT,
    CMK_VALUE_FLOAT,
    CMK_VALUE_BOOL,
    CMK_VALUE_STRING,
    CMK_VALUE_LIST,
    CMK_VALUE_ACTION,
    CMK_VALUE_TYPE_COUNT
} CMKValueType;

/* 语义类型：list 的 element 非 LIST/VOID（空列表在上下文确定前可为 VOID） */
typedef struct cmk_type {
    CMKValueType kind;
    CMKValueType element;
} CMKType;

typedef struct cmk_string {
    char *data;       /* 总是 NUL 结尾；空字符串为非 NULL 的 1 字节缓冲区 */
    CCUINT32 len;
} CMKString;

typedef struct cmk_value CMKValue;
typedef struct cmk_list CMKList;
typedef struct cmk_action CMKAction;

struct cmk_value {
    CMKValueType type;
    union {
        CCINT64 integer;
        double floating;
        CCBOOL boolean;
        CMKString string;
        CMKList *list;
        CMKAction *action;
    } as;
};

struct cmk_list {
    CMKValueType element_type;
    CCDyn elements;   /* CMKValue[] */
};

typedef enum cmk_action_kind {
    CMK_ACTION_EXEC = 0,
    CMK_ACTION_COPY,
    CMK_ACTION_MOVE,
    CMK_ACTION_REMOVE,
    CMK_ACTION_MKDIR,
    CMK_ACTION_KIND_COUNT
} CMKActionKind;

struct cmk_action {
    CMKActionKind kind;
    CMKString program;
    CMKList *args;    /* list<string>，可为 NULL */
    CMKString cwd;
    CMKList *env;     /* list<string>，可为 NULL */
    CMKString from;
    CMKString to;
    CMKString path;
};

/* ---- 类型 ---- */

CMKType cmk_type_scalar(CMKValueType kind);
CMKType cmk_type_list(CMKValueType element);
CCBOOL cmk_type_equal(const CMKType *a, const CMKType *b);
CCBOOL cmk_type_is_numeric(const CMKType *type);
CCBOOL cmk_type_is_scalar(const CMKType *type);
const char *cmk_value_type_name(CMKValueType type);
/* 写入如 "list<string>"；容量不足返回 CCFALSE */
CCBOOL cmk_type_format(
    const CMKType *type, char *buf, CCUINT32 size
);

/* ---- 字符串 ---- */

void cmk_string_init(CMKString *string);
CCBOOL cmk_string_set(
    CMKString *string, const char *data, CCUINT32 len
);
void cmk_string_clear(CMKString *string);
CCBOOL cmk_string_equal(const CMKString *a, const CMKString *b);
/* 追加一段字节；失败保持原字符串不变 */
CCBOOL cmk_string_append(
    CMKString *string, const char *data, CCUINT32 len
);

/* ---- 列表 ---- */

CMKList *cmk_list_create(CMKValueType element_type);
void cmk_list_destroy(CMKList *list);
CCUINT32 cmk_list_count(const CMKList *list);
const CMKValue *cmk_list_at(const CMKList *list, CCUINT32 index);
CCBOOL cmk_list_append(CMKList *list, const CMKValue *value);

/* ---- 值 ---- */

void cmk_value_init(CMKValue *value);
void cmk_value_clear(CMKValue *value);
CCBOOL cmk_value_copy(CMKValue *dst, const CMKValue *src);

CCBOOL cmk_value_set_void(CMKValue *value);
CCBOOL cmk_value_set_int(CMKValue *value, CCINT64 integer);
CCBOOL cmk_value_set_float(CMKValue *value, double floating);
CCBOOL cmk_value_set_bool(CMKValue *value, CCBOOL boolean);
CCBOOL cmk_value_set_string(
    CMKValue *value, const char *data, CCUINT32 len
);
CCBOOL cmk_value_set_list(CMKValue *value, CMKValueType element_type);
CCBOOL cmk_value_set_action(CMKValue *value, CMKActionKind kind);

/* 成功返回 true；类型不匹配返回 false */
CCBOOL cmk_value_as_bool(const CMKValue *value, CCBOOL *out);
CCBOOL cmk_value_as_int(const CMKValue *value, CCINT64 *out);
CCBOOL cmk_value_as_float(const CMKValue *value, double *out);
CCBOOL cmk_value_as_string(const CMKValue *value, CMKString *out);

/* 诊断/日志用文本；字符串不加引号，列表用逗号连接，动作输出 <action> */
CCBOOL cmk_value_to_string(const CMKValue *value, CMKString *out);

CCBOOL cmk_value_equal(const CMKValue *a, const CMKValue *b);

#endif
