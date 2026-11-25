/*
 * @Description: CCMake 应用层构建驱动
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "build.h"

#include "ccmake_header.h"
#include "ccvar.h"
#include "mk/mk_program.h"

#include <color_print.h>

CCBOOL build_project(void)
{
    if (!ccmake_global_var.mk_path)
        return CCTRUE;
    // --dump-config 只导出 .mc，不执行 .mk Configure
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

    MKDiagnostic diagnostic;
    mk_diagnostic_init(&diagnostic);
    MKProgram *program = mk_program_create(tree, &diagnostic);
    if (!program)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "构建脚本解析失败：程序上下文初始化失败\n"
        );
        mk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    CCBOOL ok = mk_program_load(
        program, ccmake_global_var.mk_path,
        ccmake_global_var.work_dir
    );
    if (ok)
        ok = mk_program_configure(program);
    if (!ok)
    {
        mk_diagnostic_print(&diagnostic, "构建脚本");
        mk_program_destroy(program);
        mk_diagnostic_clear(&diagnostic);
        return CCFALSE;
    }

    ok = mk_program_run_main(program);
    if (!ok)
        mk_diagnostic_print(&diagnostic, "构建");

    mk_program_destroy(program);
    mk_diagnostic_clear(&diagnostic);
    return ok;
}
