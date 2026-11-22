/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 程序入口点
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "ccmake_header.h"
#include "ccvar.h"

int main(int argc, char **argv)
{
    // 步骤一，初始化命令行状态
    init_global_var();
    // 步骤二，命令行解析
    if (!parse_cmd(argc, argv))
        goto FailedtoParseCmd;
    if (ccmake_global_var.stat == CC_CMD_FLAG_ACTION_DONE)
        goto Done;

    // 步骤三，创建全局变量树；生命周期与 Done/Failed 的 deinit 成对
    if (!ccvar_global_init())
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件解析失败：全局变量树初始化失败\n"
        );
        goto FailedtoInitGlobal;
    }
    // 步骤四，配置文件解析
    if (!parse_ccm())
        goto FailedtoParseConfig;
    // 步骤五，构建脚本解析与 Configure 求值
    if (!parse_cmk())
        goto FailedtoParseConfig;

Done:
    clear_global_var();
    ccvar_global_deinit();
    return 0;
FailedtoParseConfig:
FailedtoInitGlobal:
FailedtoParseCmd:
    clear_global_var();
    ccvar_global_deinit();
    return 1;
}