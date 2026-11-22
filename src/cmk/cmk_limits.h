/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: Parser 的实现资源预算
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_LIMITS_H_
#define _INCLUDE_CMK_LIMITS_H_

#include <crystal_cluster.h>

/*
 * 0 表示不限制，仅受内存与表示边界约束。
 * 计数类预算默认 0；语法/表达式递归深度默认开启，用于保护宿主调用栈。
 */
typedef struct cmk_limits {
    CCUINT32 max_tokens;
    CCUINT32 max_ast_nodes;
    CCUINT32 max_syntax_depth;
    CCUINT32 max_expression_depth;
    CCUINT32 max_function_params;
    CCUINT32 max_list_elements;
    CCUINT32 max_declarations;
} CMKLimits;

/* 写入默认预算；limits 为 NULL 时忽略 */
void cmk_limits_init(CMKLimits *limits);

#endif
