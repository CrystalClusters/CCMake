/*
 * @Description: CCMake 应用层构建驱动
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "build.h"

#include "ccmake_header.h"
#include "ccvar.h"
#include "cmk/cmk_program.h"

#include <color_print.h>

CCBOOL build_project(void)
{
    if (!ccmake_global_var.cmk_path)
        return CCTRUE;
    // --dump-config 只导出 .ccm，不执行 .cmk Configure
    if (ccmake_global_var.action == CC_CMD_ACTION_DUMP_CONFIG)
        return CCTRUE;

    CCVarTree *tree = ccvar_global();
    if (!tree)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "构建脚本解析失败：全局变量树未初始化\n"
        );
        return CCFALSE;
    }

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKProgram *program = cmk_program_create(tree, &diagnostic);
    if (!program)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "构建脚本解析失败：程序上下文初始化失败\n"
        );
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    CCBOOL ok = cmk_program_load(
        program, ccmake_global_var.cmk_path,
        ccmake_global_var.work_dir
    );
    if (ok)
        ok = cmk_program_configure(program);
    if (!ok)
    {
        cmk_diagnostic_print(&diagnostic, "构建脚本");
        cmk_program_destroy(program);
        cmk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    ok = cmk_program_run_main(program);
    if (!ok)
        cmk_diagnostic_print(&diagnostic, "构建");

    cmk_program_destroy(program);
    cmk_diagnostic_clear(&diagnostic);
    return ok;
}
