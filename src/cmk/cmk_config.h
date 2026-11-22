/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 语义/运行时访问 .ccm 配置树的适配层
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_CONFIG_H_
#define _INCLUDE_CMK_CONFIG_H_

#include "cmk_value.h"

typedef struct cc_var_tree CCVarTree;

typedef enum cmk_config_result {
    CMK_CONFIG_OK = 0,
    CMK_CONFIG_MISSING,
    CMK_CONFIG_TYPE,
    CMK_CONFIG_MIXED,
    CMK_CONFIG_NOMEM
} CMKConfigResult;

typedef struct cmk_config {
    CCVarTree *tree;
} CMKConfig;

void cmk_config_init(CMKConfig *config, CCVarTree *tree);

/*
 * 查询配置路径的 CMK 类型。
 * 空数组返回 list<void>；非空数组要求元素为同一标量类型。
 */
CMKConfigResult cmk_config_type(
    const CMKConfig *config, const char *path, CMKType *out
);

/*
 * 查询配置值。expected_element 只用于空数组；传 CMK_VALUE_VOID 表示未知。
 * 非空数组的元素类型由内容决定，且必须与 expected_element 兼容。
 */
CMKConfigResult cmk_config_value(
    const CMKConfig *config, const char *path,
    CMKValueType expected_element, CMKValue *out
);

#endif
