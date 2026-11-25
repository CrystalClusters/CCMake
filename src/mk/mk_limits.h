/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: Parser 的实现资源预算
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_LIMITS_H_
#define _INCLUDE_MK_LIMITS_H_

#include <crystal_cluster.h>

/*
 * 0 表示不限制，仅受内存与表示边界约束。
 * 所有字段默认 0；深度字段保留为可选执行策略。
 * Parser 的显式解析帧栈迁移见 docs/dev/资源管理原则.md。
 */
typedef struct mk_limits {
    CCUINT32 max_tokens;
    CCUINT32 max_ast_nodes;
    CCUINT32 max_syntax_depth;
    CCUINT32 max_expression_depth;
    CCUINT32 max_function_params;
    CCUINT32 max_list_elements;
    CCUINT32 max_declarations;
} MKLimits;

/* 写入默认预算；limits 为 NULL 时忽略 */
void mk_limits_init(MKLimits *limits);

#endif
