/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 语义/运行时访问 .mc 配置树的适配层
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_CONFIG_H_
#define _INCLUDE_MK_CONFIG_H_

#include "mk_value.h"

typedef struct cc_var_tree CCVarTree;

typedef enum mk_config_result {
    MK_CONFIG_OK = 0,
    MK_CONFIG_MISSING,
    MK_CONFIG_TYPE,
    MK_CONFIG_MIXED,
    MK_CONFIG_NOMEM
} MKConfigResult;

typedef struct mk_config {
    CCVarTree *tree;
} MKConfig;

void mk_config_init(MKConfig *config, CCVarTree *tree);

/*
 * 查询配置路径的 MK 类型。
 * 空数组返回 list<void>；非空数组要求元素为同一标量类型。
 */
MKConfigResult mk_config_type(
    const MKConfig *config, const char *path, MKType *out
);

/*
 * 查询配置值。expected_element 只用于空数组；传 MK_VALUE_VOID 表示未知。
 * 非空数组的元素类型由内容决定，且必须与 expected_element 兼容。
 */
MKConfigResult mk_config_value(
    const MKConfig *config, const char *path,
    MKValueType expected_element, MKValue *out
);

#endif
