/*
 * @Description: MK 执行策略默认值
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_policy.h"

#include <string.h>

void mk_execution_policy_init(MKExecutionPolicy *policy)
{
    if (policy)
        memset(policy, 0, sizeof(*policy));
}
