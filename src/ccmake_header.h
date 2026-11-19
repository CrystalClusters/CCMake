/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 内部公共头文件
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CCMAKE_HEADER_H_
#define _INCLUDE_CCMAKE_HEADER_H_

#include <color_print.h>
#include <ccalloc.h>
#include <ccfiles.h>

#include <string.h>

struct ccmake_global_var_struct{
    char *self_path;
    char *ccm_path;
    char *cmk_path;
};
extern struct ccmake_global_var_struct ccmake_global_var;

// 提供命令行参数解析
CCBOOL parse_cmd(int argc, char **argv);

#endif