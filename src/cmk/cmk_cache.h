/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 增量签名缓存（M8）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_CACHE_H_
#define _INCLUDE_CMK_CACHE_H_

#include "cmk_ir.h"

typedef struct cmk_cache CMKCache;

CMKCache *cmk_cache_create(
    const char *path, CMKDiagnostic *diagnostic
);
void cmk_cache_destroy(CMKCache *cache);

/* force 对应 -B；跳过时 *skip 为 CCTRUE */
CCBOOL cmk_cache_should_skip(
    CMKCache *cache, const CMKPlan *plan, CMKTargetId target,
    CCBOOL force, CCBOOL *skip
);
/* 执行成功后写入签名；输出文件必须存在 */
CCBOOL cmk_cache_record(
    CMKCache *cache, const CMKPlan *plan, CMKTargetId target
);
CCUINT64 cmk_cache_signature(
    const CMKPlan *plan, CMKTargetId target
);

#endif
