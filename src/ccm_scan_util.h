/*
 * @Description: CCM 各扫描阶段共享的字符串与 ${...} 边界扫描接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCM_SCAN_UTIL_H_
#define _INCLUDE_CCM_SCAN_UTIL_H_

#include <crystal_cluster.h>

/**
 * 共享扫描基元。
 * quote_pos 指向起始双引号；open_pos 指向 ${ 后的左花括号。
 * 成功时 *end 指向闭合字符，调用方自行推进到 end + 1。
 */
CCBOOL ccm_scan_string_end(
    const char *data,
    CCUINT64 size,
    CCUINT64 quote_pos,
    CCUINT64 *end
);

CCBOOL ccm_scan_builtin_end(
    const char *data,
    CCUINT64 size,
    CCUINT64 open_pos,
    CCUINT64 *end
);

#endif
