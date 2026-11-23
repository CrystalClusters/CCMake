/*
 * @Description: CMK 执行策略默认值
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_policy.h"

#include <string.h>

void cmk_execution_policy_init(CMKExecutionPolicy *policy)
{
    if (policy)
        memset(policy, 0, sizeof(*policy));
}
