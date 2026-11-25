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
#include "ccmake_console.h"
#include <ccalloc.h>
#include <ccfiles.h>
#include <cc_dyn.h>

#include <string.h>

#define CCMAKE_VER_MAJOR (CCUINT32)0
#define CCMAKE_VER_MINOR (CCUINT32)0
#define CCMAKE_VER_BUILD (CCUINT32)1

// 平台名称（供 --version 输出使用）
#ifdef CC_WINDOWS
#  define CCMAKE_PLATFORM_NAME "Windows"
#elif defined CC_LINUX
#  define CCMAKE_PLATFORM_NAME "Linux"
#elif defined CC_ANDROID
#  define CCMAKE_PLATFORM_NAME "Android"
#else
#  define CCMAKE_PLATFORM_NAME "未知平台"
#endif

// CPU 架构名称由构建脚本显式传入，不读取编译器预定义宏
#if defined(CC_ARCH_AMD64)
#  define CCMAKE_ARCH_NAME "AMD64"
#elif defined(CC_ARCH_ARM64)
#  define CCMAKE_ARCH_NAME "ARM64"
#elif defined(CC_ARCH_X86)
#  define CCMAKE_ARCH_NAME "X86"
#elif defined(CC_ARCH_ARM)
#  define CCMAKE_ARCH_NAME "ARM"
#else
#  define CCMAKE_ARCH_NAME "未知架构"
#endif

typedef enum {
    CC_CMD_FLAG_ACTION_DONE,  // 已执行动作类参数
    CC_CMD_FLAG_NEED_BUILD    // 需要执行构建
} CC_CMD_PARSE_STAT;

// 需要配置的动作类参数：解析阶段只记录，由后续流程执行
typedef enum {
    CC_CMD_ACTION_NONE,
    CC_CMD_ACTION_DUMP_CONFIG
} CC_CMD_ACTION;

// -D / --define 的单个键值对
typedef struct ccmake_define_struct {
    char *key;    // 键（ccalloc 分配）
    char *value;  // 值（ccalloc 分配）
} CCMakeDefine;

struct ccmake_global_var_struct {
    char *self_path;
    char *mc_path;
    char *mk_path;
    char *work_dir;          // -C / --directory
    char *output_dir;        // -o / --output
    CC_CMD_PARSE_STAT stat;  // 状态枚举，决定后续流程走哪个分支
    CCBOOL  verbose;         // -v / --verbose
    CCDyn   defines;         // -D / --define：CCMakeDefine 动态数组，重复键以后来者为准
    CC_CMD_ACTION action;    // 需要配置的动作类参数（如 --dump-config），由后续流程执行
};
extern struct ccmake_global_var_struct ccmake_global_var;

// 提供命令行参数解析
CCBOOL parse_cmd(int argc, char **argv);

// 命令行全局状态的成对生命周期入口
void init_global_var(void);
void clear_global_var(void);

// 提供配置文件解析
CCBOOL parse_mc();
// 提供构建脚本解析与 Configure 求值
#endif