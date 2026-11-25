/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 模块加载与 Configure 入口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_PROGRAM_H_
#define _INCLUDE_MK_PROGRAM_H_

#include "mk_config.h"
#include "mk_runtime.h"

typedef struct mk_program MKProgram;
typedef struct mk_program_module MKProgramModule;

MKProgram *mk_program_create(
    CCVarTree *tree, MKDiagnostic *diagnostic
);
void mk_program_destroy(MKProgram *program);

/* 加载根模块与全部 @import 模块，完成语义分析 */
CCBOOL mk_program_load(
    MKProgram *program, const char *root_path, const char *work_dir
);
/* 初始化各模块全局变量 */
CCBOOL mk_program_configure(MKProgram *program);
/* 执行固定入口 main()；没有 main 时直接成功 */
CCBOOL mk_program_run_main(MKProgram *program);
/* 设置 Configure 执行策略；应在 mk_program_configure 前调用 */
void mk_program_set_policy(
    MKProgram *program, const MKExecutionPolicy *policy
);
void mk_program_get_policy(
    const MKProgram *program, MKExecutionPolicy *policy
);

const MKProgramModule *mk_program_root_module(
    const MKProgram *program
);
const char *mk_program_module_path(const MKProgramModule *module);
CCUINT32 mk_program_module_count(const MKProgram *program);
const MKProgramModule *mk_program_module_at(
    const MKProgram *program, CCUINT32 index
);

CCBOOL mk_program_get_global(
    const MKProgram *program,
    const char *name, CCUINT32 name_len,
    MKValue *out
);

/* main 流程入口：加载并 Configure 当前 -f 文件 */
#endif
