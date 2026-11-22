/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 提供对.ccm文件的解析
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "ccm_internal.h"
#include "ccvar.h"

CCBOOL parse_ccm()
{
    CCVarTree *vars = ccvar_global();
    if (!vars)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件解析失败：全局变量树未初始化\n"
        );
        return CCFALSE;
    }

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

    // 第二阶段：按解析顺序解析内容，写入全局变量树
    if (ok)
        ok = ccm_parse(&ctx);

    ccm_context_clear(&ctx);
    if (!ok)
        ccvar_clear(vars);
    return ok;
}
