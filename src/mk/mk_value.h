/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 值模型、类型、列表、动作与目标引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_VALUE_H_
#define _INCLUDE_MK_VALUE_H_

#include <cc_dyn.h>
#include <crystal_cluster.h>

/* 语言值类型；MK_VALUE_VOID 也用于未解析的空列表元素类型 */
typedef enum mk_value_type {
    MK_VALUE_VOID = 0,
    MK_VALUE_INT,
    MK_VALUE_FLOAT,
    MK_VALUE_BOOL,
    MK_VALUE_STRING,
    MK_VALUE_LIST,
    MK_VALUE_TYPE_COUNT
} MKValueType;

/* 语义类型：list 的 element 非 LIST/VOID（空列表在上下文确定前可为 VOID） */
typedef struct mk_type {
    MKValueType kind;
    MKValueType element;
} MKType;

typedef struct mk_string {
    char *data;       /* 总是 NUL 结尾；空字符串为非 NULL 的 1 字节缓冲区 */
    CCUINT32 len;
} MKString;

typedef struct mk_value MKValue;
typedef struct mk_list MKList;
typedef struct mk_action MKAction;

struct mk_value {
    MKValueType type;
    union {
        CCINT64 integer;
        double floating;
        CCBOOL boolean;
        MKString string;
        MKList *list;
    } as;
};

struct mk_list {
    MKValueType element_type;
    CCDyn elements;   /* MKValue[] */
};

typedef enum mk_action_kind {
    MK_ACTION_EXEC = 0,
    MK_ACTION_COPY,
    MK_ACTION_MOVE,
    MK_ACTION_REMOVE,
    MK_ACTION_MKDIR,
    MK_ACTION_KIND_COUNT
} MKActionKind;

struct mk_action {
    MKActionKind kind;
    MKString program;
    MKList *args;    /* list<string>，可为 NULL */
    MKString cwd;
    MKList *env;     /* list<string>，可为 NULL */
    MKString from;
    MKString to;
    MKString path;
};

/* ---- 类型 ---- */

MKType mk_type_scalar(MKValueType kind);
MKType mk_type_list(MKValueType element);
CCBOOL mk_type_equal(const MKType *a, const MKType *b);
CCBOOL mk_type_is_numeric(const MKType *type);
CCBOOL mk_type_is_scalar(const MKType *type);
const char *mk_value_type_name(MKValueType type);
/* 写入如 "list<string>"；容量不足返回 CCFALSE */
CCBOOL mk_type_format(
    const MKType *type, char *buf, CCUINT32 size
);

/* ---- 字符串 ---- */

void mk_string_init(MKString *string);
CCBOOL mk_string_set(
    MKString *string, const char *data, CCUINT32 len
);
void mk_string_clear(MKString *string);
CCBOOL mk_string_equal(const MKString *a, const MKString *b);
/* 追加一段字节；失败保持原字符串不变 */
CCBOOL mk_string_append(
    MKString *string, const char *data, CCUINT32 len
);

/* ---- 列表 ---- */

MKList *mk_list_create(MKValueType element_type);
void mk_list_destroy(MKList *list);
CCUINT32 mk_list_count(const MKList *list);
const MKValue *mk_list_at(const MKList *list, CCUINT32 index);
CCBOOL mk_list_append(MKList *list, const MKValue *value);

/* ---- 值 ---- */

void mk_value_init(MKValue *value);
void mk_value_clear(MKValue *value);
CCBOOL mk_value_copy(MKValue *dst, const MKValue *src);

CCBOOL mk_value_set_void(MKValue *value);
CCBOOL mk_value_set_int(MKValue *value, CCINT64 integer);
CCBOOL mk_value_set_float(MKValue *value, double floating);
CCBOOL mk_value_set_bool(MKValue *value, CCBOOL boolean);
CCBOOL mk_value_set_string(
    MKValue *value, const char *data, CCUINT32 len
);
CCBOOL mk_value_set_list(MKValue *value, MKValueType element_type);

/* 成功返回 true；类型不匹配返回 false */
CCBOOL mk_value_as_bool(const MKValue *value, CCBOOL *out);
CCBOOL mk_value_as_int(const MKValue *value, CCINT64 *out);
CCBOOL mk_value_as_float(const MKValue *value, double *out);
CCBOOL mk_value_as_string(const MKValue *value, MKString *out);

/* 诊断/日志用文本；字符串不加引号，列表用逗号连接，动作输出 <action> */
CCBOOL mk_value_to_string(const MKValue *value, MKString *out);

CCBOOL mk_value_equal(const MKValue *a, const MKValue *b);

#endif
