/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: Parser 的实现资源预算
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_limits.h"

void mk_limits_init(MKLimits *limits)
{
    if (!limits)
        return;

    limits->max_tokens = 0;
    limits->max_ast_nodes = 0;
    limits->max_syntax_depth = 0;
    limits->max_expression_depth = 0;
    limits->max_function_params = 0;
    limits->max_list_elements = 0;
    limits->max_declarations = 0;
}
