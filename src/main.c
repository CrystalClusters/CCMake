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
    // 一定要记得清空内存脏数据
    memset(&ccmake_global_var, 0, sizeof(ccmake_global_var));
    // 步骤一，命令行解析
    if (!parse_cmd(argc, argv))
        goto FailedtoParseCmd;
    if (ccmake_global_var.stat == CC_CMD_FLAG_ACTION_DONE)
        goto ActionDone;
    // 步骤二，配置文件解析
    if (!parse_ccm())
        goto FailedtoParseConfig;
    color_print(
        "%s\n%s\n%s\n",
        ccmake_global_var.self_path,
        ccmake_global_var.ccm_path,
        ccmake_global_var.cmk_path
    );
ActionDone:
    clear_global_var();
    ccvar_global_deinit();
    return 0;
FailedtoParseConfig:
FailedtoParseCmd:
    clear_global_var();
    ccvar_global_deinit();
    return 1;
}