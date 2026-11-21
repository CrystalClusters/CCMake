/*
 * @Description: CCMake 测试状态、统计与断言输出（参考 CrystalStd 测试结构）。
 *               不维护用例注册表；用例编号只在测试代码的 CC_CASE 中显式出现。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include <color_print.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

/* ============================================================================
 * stdout 捕获
 * ============================================================================ */

void cc_test_strip_ansi(char *text)
{
    char *dst = text;
    const char *src = text;

    while (*src)
    {
        if ((unsigned char)src[0] == 0x1B && src[1] == '[')
        {
            src += 2;
            while (*src && !((*src >= 'A' && *src <= 'Z') ||
                             (*src >= 'a' && *src <= 'z')))
                src++;
            if (*src) src++;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

CCBOOL cc_test_capture_begin(cc_test_capture *cap)
{
    int fds[2];

    if (!cap)
        return CCFALSE;

    fflush(stdout);
#ifdef CC_WINDOWS
    if (_pipe(fds, 65536, _O_BINARY) != 0)
    {
        fprintf(stderr, "[HARNESS] _pipe 失败，errno=%d\n", errno);
        return CCFALSE;
    }
    cap->saved = _dup(_fileno(stdout));
    if (cap->saved < 0)
    {
        fprintf(stderr, "[HARNESS] _dup 失败，errno=%d\n", errno);
        _close(fds[0]);
        _close(fds[1]);
        return CCFALSE;
    }
    if (_dup2(fds[1], _fileno(stdout)) != 0)
    {
        fprintf(stderr, "[HARNESS] _dup2 失败，errno=%d\n", errno);
        _close(fds[0]);
        _close(fds[1]);
        _close(cap->saved);
        return CCFALSE;
    }
    _close(fds[1]);
#else
    if (pipe(fds) != 0)
    {
        fprintf(stderr, "[HARNESS] pipe 失败，errno=%d\n", errno);
        return CCFALSE;
    }
    cap->saved = dup(fileno(stdout));
    if (cap->saved < 0)
    {
        fprintf(stderr, "[HARNESS] dup 失败，errno=%d\n", errno);
        close(fds[0]);
        close(fds[1]);
        return CCFALSE;
    }
    if (dup2(fds[1], fileno(stdout)) < 0)
    {
        fprintf(stderr, "[HARNESS] dup2 失败，errno=%d\n", errno);
        close(fds[0]);
        close(fds[1]);
        close(cap->saved);
        return CCFALSE;
    }
    close(fds[1]);
#endif
    cap->read_fd = fds[0];
    return CCTRUE;
}

void cc_test_capture_end(cc_test_capture *cap, char *out, size_t cap_size)
{
    size_t total = 0;

    if (!cap || !out || cap_size == 0)
        return;

    fflush(stdout);
#ifdef CC_WINDOWS
    _dup2(cap->saved, _fileno(stdout));
    _close(cap->saved);
    while (total < cap_size - 1)
    {
        int got = _read(
            cap->read_fd, out + total,
            (unsigned int)(cap_size - 1 - total)
        );
        if (got <= 0) break;
        total += (size_t)got;
    }
    _close(cap->read_fd);
#else
    dup2(cap->saved, fileno(stdout));
    close(cap->saved);
    while (total < cap_size - 1)
    {
        ssize_t got = read(cap->read_fd, out + total, cap_size - 1 - total);
        if (got <= 0) break;
        total += (size_t)got;
    }
    close(cap->read_fd);
#endif
    out[total] = '\0';
    cc_test_strip_ansi(out);
}

#define CC_TEST_NAME_MAX 192
#define CC_TEST_DETAIL_MAX 512

static const char *g_name = "CCMake";

static CCINT32 g_pass = 0;
static CCINT32 g_fail = 0;
static CCUINT32 g_checks = 0;

static CCINT32 g_suite_pass = 0;
static CCINT32 g_suite_fail = 0;
static CCUINT32 g_suite_checks = 0;

static char g_suite[64] = "TEST";

static CCBOOL g_case_active = CCFALSE;
static char g_case_id[64] = "";
static char g_case_name[CC_TEST_NAME_MAX] = "";
static int g_case_checks = 0;
static int g_case_failed = 0;

static void format_detail(
    char *out,
    CCUINT32 cap,
    const char *label,
    const char *expected,
    const char *actual
)
{
    const char *name = (label && *label) ? label : "(unnamed)";
    if (expected && actual)
        snprintf(out, cap, "%s: 期望 %s，实际 %s", name, expected, actual);
    else if (expected)
        snprintf(out, cap, "%s: 期望 %s", name, expected);
    else if (actual)
        snprintf(out, cap, "%s: 实际 %s", name, actual);
    else
        snprintf(out, cap, "%s", name);
}

void cc_test_begin(const char *name)
{
    if (name && *name)
        g_name = name;

    g_pass = 0;
    g_fail = 0;
    g_checks = 0;
    g_suite_pass = 0;
    g_suite_fail = 0;
    g_suite_checks = 0;
    g_case_active = CCFALSE;

    color_print(CC_TEXT_COLOR(CC_BCYAN, CC_DEFAULT) "== %s 测试 ==\n", g_name);
    fflush(stdout);
}

void cc_test_suite_begin(const char *suite, const char *title)
{
    if (g_case_active)
        cc_test_case_end();

    snprintf(g_suite, sizeof(g_suite), "%s", (suite && *suite) ? suite : "TEST");
    g_suite_pass = 0;
    g_suite_fail = 0;
    g_suite_checks = 0;

    color_print(
        CC_TEXT_COLOR(CC_BMAGENTA, CC_DEFAULT) "\n== %s (%s) ==\n",
        (title && *title) ? title : g_suite, g_suite
    );
    fflush(stdout);
}

void cc_test_suite_end(void)
{
    if (g_case_active)
        cc_test_case_end();

    color_print(
        CC_TEXT_COLOR(CC_BCYAN, CC_DEFAULT) "[%s] total=%d passed=%d failed=%d checks=%u\n",
        g_suite,
        g_suite_pass + g_suite_fail,
        g_suite_pass, g_suite_fail, g_suite_checks
    );
    fflush(stdout);
}

void cc_test_case_begin(
    const char *id,
    const char *name,
    const char *file,
    int line
)
{
    if (g_case_active)
        cc_test_case_end();

    (void)file;
    (void)line;

    snprintf(g_case_id, sizeof(g_case_id), "%s", (id && *id) ? id : "CASE");
    snprintf(
        g_case_name, sizeof(g_case_name), "%s",
        (name && *name) ? name : g_case_id);
    g_case_checks = 0;
    g_case_failed = 0;
    g_case_active = CCTRUE;
}

void cc_test_check(
    CCBOOL condition,
    const char *label,
    const char *expected,
    const char *actual,
    const char *file,
    int line
)
{
    char detail[CC_TEST_DETAIL_MAX];

    (void)file;
    (void)line;

    if (!g_case_active)
        return;

    g_case_checks++;
    g_suite_checks++;
    g_checks++;
    if (condition)
        return;

    g_case_failed++;
    format_detail(detail, sizeof(detail), label, expected, actual);
    color_print(
        CC_TEXT_COLOR(CC_BRED, CC_DEFAULT) "[%s][失败] %s\n",
        g_case_id, detail
    );
    fflush(stdout);
}

void cc_test_case_end(void)
{
    if (!g_case_active)
        return;
    g_case_active = CCFALSE;

    if (g_case_failed > 0)
    {
        g_suite_fail++;
        g_fail++;
        color_print(
            CC_TEXT_COLOR(CC_BRED, CC_DEFAULT) "[%s][失败] %s（%d 项断言，%d 项失败）\n",
            g_case_id, g_case_name, g_case_checks, g_case_failed
        );
    }
    else if (g_case_checks == 0)
    {
        g_suite_fail++;
        g_fail++;
        color_print(CC_TEXT_COLOR(CC_BRED, CC_DEFAULT) "[%s][失败] %s（没有断言）\n", g_case_id, g_case_name);
    }
    else
    {
        g_suite_pass++;
        g_pass++;
        color_print(
            CC_TEXT_COLOR(CC_BGREEN, CC_DEFAULT) "[%s][通过] %s（%d 项断言）\n",
            g_case_id, g_case_name, g_case_checks
        );
    }
    fflush(stdout);
}

void cc_test_print_summary(void)
{
    color_print(
        CC_TEXT_COLOR(CC_BCYAN, CC_DEFAULT) "\n# SUMMARY total=%d pass=%d fail=%d checks=%u\n",
        g_pass + g_fail, g_pass, g_fail, g_checks
    );
    fflush(stdout);
}

CCINT32 cc_test_failures(void) { return g_fail; }
CCINT32 cc_test_suite_failures(void) { return g_suite_fail; }
CCUINT32 cc_test_checks(void) { return g_checks; }
