/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 提供对.mc文件的解析
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mc_internal.h"
#include "ccvar.h"

CCBOOL parse_mc()
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

    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    // 第一阶段：检查并定位根文件与全部 @include 文件
    CCBOOL ok = mc_locate(
        ccmake_global_var.mc_path,
        ccmake_global_var.work_dir,
        &ctx
    );
    if (ok && ccmake_global_var.verbose)
        mc_print_locations(&ctx);

    // 第二阶段：按解析顺序解析内容，写入全局变量树
    if (ok)
        ok = mc_parse(&ctx);

    mc_context_clear(&ctx);
    if (!ok)
    {
        if (ccvar_clear(vars) != CC_VAR_OK)
            color_print(
                CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
                "配置文件解析失败：全局变量树回滚失败\n"
            );
    }
    return ok;
}
