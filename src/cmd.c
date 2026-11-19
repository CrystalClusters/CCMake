/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 提供命令行解析
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "ccmake_header.h"

static int g_index = 0;
static int g_argc = 0;
static char **g_argv = NULL;

// 处理函数的统一签名
typedef CCBOOL(*cmd_parse_func)(void);

// 指令函数速查表
typedef struct func_table_struct {
    const char *name;
    cmd_parse_func func;
} FUNC_TABLE;

static CCBOOL _func_empty(void)
{
    color_print("%c_8f空函数\n");
    g_index++;
    return CCTRUE;
}

static CCBOOL _func_config_(void)
{
    g_index++;
    if (g_index >= g_argc)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未找到config参数：%s <---\n",
            g_argv[g_index - 1]
        );
        return CCFALSE;
    }
    if (g_argv[g_index][0] == '-')
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未找到config参数：%s -><- %s\n",
            g_argv[g_index - 1], g_argv[g_index]
        );
        return CCFALSE;
    }
    // 拷贝字符串操作
    CCUINT32 len = strlen(g_argv[g_index]);
    ccmake_global_var.ccm_path = ccalloc(NULL, len + 1, CC_DBG_REL_TAG(".ccm文件路径", NULL));
    strcpy(ccmake_global_var.ccm_path, g_argv[g_index]);
    // 跳过已拷贝的参数
    g_index++;
    return CCTRUE;
}

static CCBOOL _func_file_(void)
{
    g_index++;
    if (g_index >= g_argc)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未找到file参数：%s <---\n",
            g_argv[g_index - 1]
        );
        return CCFALSE;
    }
    if (g_argv[g_index][0] == '-')
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未找到file参数：%s -><- %s\n",
            g_argv[g_index - 1], g_argv[g_index]
        );
        return CCFALSE;
    }
    // 拷贝字符串操作
    CCUINT32 len = strlen(g_argv[g_index]);
    ccmake_global_var.cmk_path = ccalloc(NULL, len + 1, CC_DBG_REL_TAG(".ccm文件路径", NULL));
    strcpy(ccmake_global_var.cmk_path, g_argv[g_index]);
    // 跳过已拷贝的参数
    g_index++;
    return CCTRUE;
}

static FUNC_TABLE func_table[] = {
    {"-c", _func_config_}, {"--config", _func_config_},
    {"-f", _func_file_}, {"--file", _func_file_},
    {"-C", _func_empty}, {"--directory", _func_empty},
    {"-j", _func_empty}, {"--jobs", _func_empty},
    {"-D", _func_empty}, {"--define", _func_empty},
    {"-o", _func_empty}, {"--output", _func_empty},
    {"-t", _func_empty}, {"--target", _func_empty},
    {"-v", _func_empty}, {"--verbose", _func_empty},
    {"-n", _func_empty}, {"--dry-run", _func_empty},
    {"-B", _func_empty}, {"--always-build", _func_empty},
    {"-k", _func_empty}, {"--keep-going", _func_empty},
    {"-h", _func_empty}, {"--help", _func_empty},
    {"-V", _func_empty}, {"--version", _func_empty}
};
#define FUNC_TABLE_SIZE (CCINT32)(sizeof(func_table) / sizeof(func_table[0]))

CCBOOL single_cmd()
{
    // 检查参数是否以“-”开始
    // 对应指令的解析函数会自动将 g_index 递增
    if (g_argv[g_index][0] == '-')
    {
        for (CCINT32 i = 0; i < FUNC_TABLE_SIZE; i++)
        {
            if (!strcmp(g_argv[g_index], func_table[i].name))
                return func_table[i].func();
        }
    }
    else
    {
        color_print("不合规的参数：%s <---\n", g_argv[g_index]);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL check_parse()
{
    if (
        !ccmake_global_var.self_path ||
        !ccmake_global_var.ccm_path ||
        !ccmake_global_var.cmk_path
    )
        return CCFALSE;
    return CCTRUE;
}

CCBOOL parse_cmd(int argc, char **argv)
{
    g_index = 1;
    g_argc = argc;
    g_argv = argv;
    CCUINT32 len = strlen(argv[0]);
    ccmake_global_var.self_path = ccalloc(NULL, len + 1, CC_DBG_REL_TAG("可执行程序自指路径", NULL));
    strcpy(ccmake_global_var.self_path, argv[0]);
    CCBOOL valid_cmd = CCTRUE;
    while (g_index < g_argc)
    {
        if (!single_cmd())
        {
            valid_cmd = CCFALSE;
            break;
        }
    }
    return check_parse() && valid_cmd;
}