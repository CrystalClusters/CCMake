/*
 * @Description: CCMake 测试入口：套件表、命令行参数与退出码。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"

#include <string.h>

typedef struct test_suite {
    const char *name;
    const char *title;
    void (*run)(void);
} test_suite;

static const test_suite g_suites[] = {
    { "CMD", "命令行解析", test_cmd },
    { "CCM", "CCM 文件检查与定位", test_ccm_locate },
    { "PARSE", "CCM 内容解析", test_ccm_parse },
    { "VARS", "全局变量树", test_ccvar },
    { "CMK", "CMK 词法分析", test_cmk_lexer }
};

#define SUITE_COUNT ((CCUINT32)(sizeof(g_suites) / sizeof(g_suites[0])))

static void inner_print_usage(void)
{
    color_print(
        CC_TEXT_COLOR(CC_BCYAN, CC_DEFAULT)
        "用法: ccmake_test [all|<套件名>] [--help]\n"
    );
    color_print("套件名: ");
    for (CCUINT32 i = 0; i < SUITE_COUNT; i++)
        color_print("%s%s", i ? "|" : "", g_suites[i].name);
    color_print("\n无参数时运行全部套件。\n");
}

static int inner_find_suite(const char *name)
{
    if (!name || !*name) return -1;
    if (!strcmp(name, "all")) return 100;
    for (CCUINT32 i = 0; i < SUITE_COUNT; i++)
        if (!strcmp(name, g_suites[i].name)) return (int)i;
    return -1;
}

static int inner_run_one(CCUINT32 index)
{
    cc_test_suite_begin(g_suites[index].name, g_suites[index].title);
    g_suites[index].run();
    cc_test_suite_end();
    return cc_test_suite_failures() ? 1 : 0;
}

static int inner_run_all(void)
{
    int failed = 0;
    for (CCUINT32 i = 0; i < SUITE_COUNT; i++)
    {
        if (inner_run_one(i)) failed = 1;
    }
    return failed;
}

int main(int argc, char **argv)
{
    const char *suite = NULL;
    int index;
    int failed;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            inner_print_usage();
            return 0;
        }
        if (argv[i][0] == '-')
        {
            color_print(
                CC_TEXT_COLOR(CC_BRED, CC_DEFAULT)
                "[错误] 未知选项: %s\n", argv[i]
            );
            inner_print_usage();
            return 2;
        }
        if (suite)
        {
            color_print(
                CC_TEXT_COLOR(CC_BRED, CC_DEFAULT)
                "[错误] 多余的套件参数: %s\n", argv[i]
            );
            inner_print_usage();
            return 2;
        }
        suite = argv[i];
    }

    if (!suite)
        suite = "all";

    index = inner_find_suite(suite);
    if (index < 0)
    {
        color_print(
            CC_TEXT_COLOR(CC_BRED, CC_DEFAULT)
            "[错误] 未知套件: %s\n", suite
        );
        inner_print_usage();
        return 2;
    }

#ifdef CC_RELEASE
    cc_test_begin("CCMake release");
#else
    cc_test_begin("CCMake debug");
#endif

    failed = (index == 100) ? inner_run_all() : inner_run_one((CCUINT32)index);

    cc_test_print_summary();
    return (failed || cc_test_failures()) ? 1 : 0;
}
