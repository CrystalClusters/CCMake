/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 全局变量树内部结构（仅 ccvar.c 使用）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCVAR_INTERNAL_H_
#define _INCLUDE_CCVAR_INTERNAL_H_

#include "ccvar.h"

#include <ccalloc.h>
#include <cc_dyn.h>
#include <cc_rbt.h>

#define CC_VAR_NIL 0xFFFFFFFFu

typedef struct cc_var_node {
    char     *name;          /* 对象成员键；数组元素 / 根为 NULL */
    CCUINT32  name_len;
    CCVarType type;

    CCUINT32  parent;        /* 层级父节点 */
    CCUINT32  children_root; /* 对象：子红黑树根 */

    CCUINT32  rbt_parent;    /* 红黑树链接 */
    CCUINT32  rbt_left;
    CCUINT32  rbt_right;
    CCBOOL    is_red;

    CCDyn     elements;      /* 数组：CCUINT32[]，顺序表 */

    union {
        CCBOOL   boolean;
        CCINT64  integer;
        double   floating;
        struct {
            char    *data;
            CCUINT32 len;
        } string;
    } value;
} CCVarNode;

struct cc_var_tree {
    CCDyn    nodes;      /* CCVarNode[] */
    CCUINT32 root;
    CCUINT32 rbt_owner;  /* 当前操作的对象节点，供 ch_root 写回 */
};

#endif
