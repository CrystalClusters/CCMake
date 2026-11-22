/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 模块加载与 Configure 入口（M3/M4）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_PROGRAM_H_
#define _INCLUDE_CMK_PROGRAM_H_

#include "cmk_config.h"
#include "cmk_runtime.h"

typedef struct cmk_program CMKProgram;
typedef struct cmk_program_module CMKProgramModule;

CMKProgram *cmk_program_create(
    CCVarTree *tree, CMKDiagnostic *diagnostic
);
void cmk_program_destroy(CMKProgram *program);

/* 加载根模块与全部 @import 模块，完成语义分析 */
CCBOOL cmk_program_load(
    CMKProgram *program, const char *root_path, const char *work_dir
);
/* 初始化各模块全局变量，并求值根模块的 target 声明 */
CCBOOL cmk_program_configure(CMKProgram *program);

CMKRuntime *cmk_program_root_runtime(const CMKProgram *program);
const CMKProgramModule *cmk_program_root_module(
    const CMKProgram *program
);
const char *cmk_program_module_path(const CMKProgramModule *module);
CCUINT32 cmk_program_module_count(const CMKProgram *program);
const CMKProgramModule *cmk_program_module_at(
    const CMKProgram *program, CCUINT32 index
);

CCBOOL cmk_program_get_global(
    const CMKProgram *program,
    const char *name, CCUINT32 name_len,
    CMKValue *out
);
CCUINT32 cmk_program_target_count(const CMKProgram *program);
const CMKTargetDraft *cmk_program_target_at(
    const CMKProgram *program, CCUINT32 index
);

/* main 流程入口：加载并 Configure 当前 -f 文件 */
CCBOOL parse_cmk(void);

#endif
