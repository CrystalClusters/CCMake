/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 程序入口点
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "ccmake_header.h"

struct ccmake_global_var_struct ccmake_global_var;

void zerzo_global_var()
{
    ccmake_global_var.self_path = NULL;
    ccmake_global_var.ccm_path = NULL;
    ccmake_global_var.cmk_path = NULL;
}

void clear_global_var()
{
    // 命令行参数是乱序排列的
    // 无法做成原子化设计，只能一个个检查有没有初始化
    if(ccmake_global_var.self_path) ccalloc(ccmake_global_var.self_path, 0, NULL);
    if(ccmake_global_var.ccm_path) ccalloc(ccmake_global_var.ccm_path, 0, NULL);
    if(ccmake_global_var.cmk_path) ccalloc(ccmake_global_var.cmk_path, 0, NULL);
}

int main(int argc, char **argv)
{

    if (!parse_cmd(argc, argv))
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "命令行参数解析失败\n");
        goto FailedtoParseCmd;
    }
    clear_global_var();
    color_print(
        "命令行初始化成功：\n%s\n%s\n%s\n",
        ccmake_global_var.self_path,
        ccmake_global_var.ccm_path,
        ccmake_global_var.cmk_path
    );
    return 0;
FailedtoParseCmd:
    clear_global_var();
    return 1;
}