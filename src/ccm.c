/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 提供对.ccm文件的解析
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "ccm_internal.h"

/* 第二阶段：内容解析。当前只完成第一阶段的检查与定位。 */
CCBOOL ccm_parse(CCMakeCCMContext *ctx)
{
    CC_UNUSED(ctx);
    color_print(
        CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
        "配置文件内容解析（第二阶段）未实现\n"
    );
    return CCFALSE;
}

CCBOOL parse_ccm()
{
    CCMakeCCMContext ctx;
    ccm_context_init(&ctx);

    // 第一阶段：检查并定位根文件与全部 @include 文件
    CCBOOL ok = ccm_locate(
        ccmake_global_var.ccm_path,
        ccmake_global_var.work_dir,
        &ctx
    );
    if (ok && ccmake_global_var.verbose)
        ccm_print_locations(&ctx);

    // 第二阶段：解析文件内容
    if (ok)
        ok = ccm_parse(&ctx);

    ccm_context_clear(&ctx);
    return ok;
}
