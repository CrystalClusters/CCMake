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

static CCBOOL _func_version_(void)
{
    ccmake_global_var.stat = CC_CMD_FLAG_ACTION_DONE;
    color_print(
        "CCMake - Crystal Cluster Make：\n"
    #ifdef CC_WINDOWS
        "Windows"
    #elif defined CC_LINUX
        "Linux"
    #elif defined CC_ANDROID
        "Android"
    #else
        "未知平台"
    #endif
        "\n版本：v%d.%d.%d\n", CCMAKE_VER_MAJOR, CCMAKE_VER_MINOR, CCMAKE_VER_BUILD
    );
    return CCTRUE;
}

static CCBOOL _func_help_(void)
{
    ccmake_global_var.stat = CC_CMD_FLAG_ACTION_DONE;
    color_print(
        "-c, --config <文件>          指定 .ccm 配置文件（必填）\n"
        "-f, --file <文件>            指定 .cmk 构建脚本（必填）\n"
        "-C, --directory <目录>       切换工作目录\n"
        "-j, --jobs <数量>            并行任务数（默认: CPU 核心数）\n"
        "-D, --define <键=值> <键=值> 覆盖配置文件字段\n"
        "-o, --output <目录>          构建产物输出目录\n"
        "-t, --target <目标>          构建目标（如 debug, release）\n"
        "-v, --verbose                详细输出\n"
        "-n, --dry-run                仅显示操作，不执行\n"
        "-B, --always-build           强制重新构建\n"
        "-k, --keep-going             遇到错误继续构建\n"
        "    --list-targets           列出可用构建目标\n"
        "    --dump-config            展开变量后输出完整配置\n"
        "-h, --help                   显示此帮助信息\n"
        "-V, --version                显示版本信息\n"
    );
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
    {"-h", _func_help_}, {"--help", _func_help_},
    {"-V", _func_version_}, {"--version", _func_version_},
    {"--list-targets", _func_empty}, {"--dump-config", _func_empty}
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
        // 没有匹配到相关指令参数
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "未知的指令参数：%s\n",
            g_argv[g_index]
        );
        _func_help_();
        return CCFALSE;
    }
    color_print(
        CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
        "不合规的参数：%s <---\n",
        g_argv[g_index]
    );
    return CCFALSE;
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
    // 没有参数的情况下默认触发 -h
    if (argc <= 1) return _func_help_();
    //
    g_index = 1;
    g_argc = argc;
    g_argv = argv;
    CCUINT32 len = strlen(argv[0]);
    ccmake_global_var.self_path = ccalloc(NULL, len + 1, CC_DBG_REL_TAG("可执行程序自指路径", NULL));
    strcpy(ccmake_global_var.self_path, argv[0]);
    CCBOOL valid_cmd = CCTRUE;
    // 默认需要构建，可被 ACTION_DONE 覆盖
    ccmake_global_var.stat = CC_CMD_FLAG_NEED_BUILD;
    while (g_index < g_argc)
    {
        if (!single_cmd())
        {
            valid_cmd = CCFALSE;
            break;
        }
        if (ccmake_global_var.stat == CC_CMD_FLAG_ACTION_DONE)
            return CCTRUE;
    }
    return check_parse() && valid_cmd;
}