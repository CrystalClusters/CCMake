/*
 * @Description: CCMake 应用层构建入口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_BUILD_H_
#define _INCLUDE_BUILD_H_

#include "ccmake_header.h"

/* CLI 构建入口：Configure → Lower → Scheduler → Executor → Cache */
CCBOOL build_project(void);

#endif
