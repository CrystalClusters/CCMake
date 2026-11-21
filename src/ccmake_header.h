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

#define CCMAKE_VER_MAJOR (CCUINT32)0
#define CCMAKE_VER_MINOR (CCUINT32)0
#define CCMAKE_VER_BUILD (CCUINT32)1

typedef enum {
    CC_CMD_FLAG_ACTION_DONE,  // 已执行动作类参数
    CC_CMD_FLAG_NEED_BUILD    // 需要执行构建
} CC_CMD_PARSE_STAT;

struct ccmake_global_var_struct{
    char *self_path;
    char *ccm_path;
    char *cmk_path;
    char *work_dir;          // -C / --directory
    char *output_dir;        // -o / --output
    char *target;            // -t / --target 或位置参数
    int   jobs;              // -j / --jobs (0 = 自动检测)
    CC_CMD_PARSE_STAT stat;  // 状态枚举，决定后续流程走哪个分支
    CCBOOL  verbose;         // -v / --verbose
    CCBOOL  dry_run;         // -n / --dry-run
    CCBOOL  always_build;    // -B / --always-build
    CCBOOL  keep_going;      // -k / --keep-going
};
extern struct ccmake_global_var_struct ccmake_global_var;

// 提供命令行参数解析
CCBOOL parse_cmd(int argc, char **argv);

#endif