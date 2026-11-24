/*
 * @Description: 命令行解析测试。直接调用 parse_cmd，校验返回值、
 *               解析结果字段与解析阶段输出。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "ccmake_header.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#define OUT_CAP 8192
#define ARGC(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ============================================================================
 * stdout 捕获与解析调用
 * ============================================================================ */

static char g_out[OUT_CAP];

typedef struct capture {
    int read_fd;
    int saved;
} capture;

/* 去掉终端颜色控制序列，保证期望文本与运行环境无关 */
static void strip_ansi(char *text)
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

static CCBOOL capture_begin(capture *cap)
{
    int fds[2];

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

static void capture_end(capture *cap, char *out, size_t cap_size)
{
    size_t total = 0;

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
    strip_ansi(out);
}

/* 清空全局状态后调用 parse_cmd，捕获其输出 */
static CCBOOL run_parse(int argc, const char *const argv[])
{
    char *args[32];
    capture cap;
    CCBOOL ret;
    int i;

    for (i = 0; i < argc && i < 31; i++)
        args[i] = (char *)argv[i];
    args[i] = NULL;

    memset(&ccmake_global_var, 0, sizeof(ccmake_global_var));
    g_out[0] = '\0';

    if (!capture_begin(&cap))
    {
        fprintf(stderr, "[HARNESS] stdout 捕获失败，输出断言不可用\n");
        g_out[0] = '\0';
        return parse_cmd(i, args);
    }

    ret = parse_cmd(i, args);
    capture_end(&cap, g_out, sizeof(g_out));
    return ret;
}

/* 捕获自检：确认当前 stdio 环境可用（不依赖临时文件） */
static CCBOOL capture_selftest(void)
{
    capture cap;

    if (!capture_begin(&cap))
        return CCFALSE;

    fputs("capture-ok\n", stdout);
    fflush(stdout);
    capture_end(&cap, g_out, sizeof(g_out));
    return (strcmp(g_out, "capture-ok\n") == 0) ? CCTRUE : CCFALSE;
}

static void case_cleanup(void)
{
    clear_global_var();
    memset(&ccmake_global_var, 0, sizeof(ccmake_global_var));
}

/* ============================================================================
 * 断言辅助
 * ============================================================================ */

static CCUINT32 count_lines(const char *text)
{
    CCUINT32 lines = 0;
    while (*text)
    {
        if (*text == '\n')
            lines++;
        text++;
    }
    return lines;
}

static CCBOOL text_has(const char *text, const char *needle)
{
    return strstr(text, needle) ? CCTRUE : CCFALSE;
}

/* 校验 “前缀 + 数字 + 后缀” 的并行任务提示，数字须等于解析出的 jobs */
static CCBOOL jobs_hint_matches(const char *prefix, const char *suffix)
{
    size_t prefix_len = strlen(prefix);
    const char *p;
    CCINT64 value = 0;
    int digits = 0;

    if (strncmp(g_out, prefix, prefix_len) != 0)
        return CCFALSE;

    p = g_out + prefix_len;
    while (*p >= '0' && *p <= '9')
    {
        value = value * 10 + (*p - '0');
        p++;
        digits++;
    }
    if (digits == 0)
        return CCFALSE;

    if (suffix)
    {
        if (strcmp(p, suffix) != 0)
            return CCFALSE;
    }
    else if (*p != '\n' || p[1] != '\0')
    {
        return CCFALSE;
    }

    return (value == (CCINT64)ccmake_global_var.jobs) ? CCTRUE : CCFALSE;
}

static CCUINT32 define_count(void)
{
    return ccmake_global_var.defines.size / (CCUINT32)sizeof(CCMakeDefine);
}

static CCBOOL define_at(CCUINT32 index, const char **key, const char **value)
{
    CCMakeDefine def;

    if (index >= define_count())
        return CCFALSE;
    if (cc_dyn_core_seek(
            &ccmake_global_var.defines,
            (CCUINT32)sizeof(CCMakeDefine),
            (CCINT32)sizeof(CCMakeDefine),
            &def, index
        ) != 0
    ) return CCFALSE;

    *key = def.key;
    *value = def.value;
    return CCTRUE;
}

static void make_version_text(char *buf, size_t cap)
{
    snprintf(
        buf, cap,
        "CCMake - Crystal Cluster Make：\n"
        "%s %s\n"
        "版本：v%d.%d.%d\n",
        CCMAKE_PLATFORM_NAME, CCMAKE_ARCH_NAME,
        (int)CCMAKE_VER_MAJOR, (int)CCMAKE_VER_MINOR, (int)CCMAKE_VER_BUILD
    );
}

/* ============================================================================
 * 帮助与版本
 * ============================================================================ */

static void test_no_args(void)
{
    const char *argv[] = { "ccmake" };
    CCBOOL ret;

    CC_CASE("CMD-NOARGS-001", "无参数默认输出帮助");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("stat", CC_CMD_FLAG_ACTION_DONE, ccmake_global_var.stat);
    CC_CHECK_EQ_I64("正文行数", 13, count_lines(g_out));
    CC_CHECK(text_has(g_out, "-c, --config"), "包含 -c 选项行");
    CC_CHECK(text_has(g_out, "-V, --version"), "包含 -V 选项行");
    CC_CHECK(ccmake_global_var.self_path == NULL, "未设置 self_path");
    CC_CASE_END();
    case_cleanup();
}

static void test_help_short(void)
{
    const char *argv[] = { "ccmake", "-h" };
    CCBOOL ret;

    CC_CASE("CMD-HELP-001", "-h 输出帮助并标记动作完成");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("stat", CC_CMD_FLAG_ACTION_DONE, ccmake_global_var.stat);
    CC_CHECK_EQ_I64("正文行数", 13, count_lines(g_out));
    CC_CHECK(text_has(g_out, "-c, --config"), "包含 -c 选项行");
    CC_CHECK(text_has(g_out, "-D, --define"), "包含 -D 选项行");
    CC_CHECK(text_has(g_out, "-V, --version"), "包含 -V 选项行");
    CC_CHECK_EQ_STR("self_path", "ccmake", ccmake_global_var.self_path);
    CC_CHECK(ccmake_global_var.ccm_path == NULL, "未设置 ccm_path");
    CC_CASE_END();
    case_cleanup();
}

static void test_help_long(void)
{
    const char *argv[] = { "ccmake", "--help" };
    CCBOOL ret;

    CC_CASE("CMD-HELP-002", "--help 与 -h 等价");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("stat", CC_CMD_FLAG_ACTION_DONE, ccmake_global_var.stat);
    CC_CHECK_EQ_I64("正文行数", 13, count_lines(g_out));
    CC_CASE_END();
    case_cleanup();
}

static void test_version_short(void)
{
    const char *argv[] = { "ccmake", "-V" };
    char want[256];
    CCBOOL ret;

    make_version_text(want, sizeof(want));

    CC_CASE("CMD-VERSION-001", "-V 输出平台、架构与版本号");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("stat", CC_CMD_FLAG_ACTION_DONE, ccmake_global_var.stat);
    CC_CHECK_EQ_STR("版本输出", want, g_out);
    CC_CASE_END();
    case_cleanup();
}

static void test_version_long(void)
{
    const char *argv[] = { "ccmake", "--version" };
    char want[256];
    CCBOOL ret;

    make_version_text(want, sizeof(want));

    CC_CASE("CMD-VERSION-002", "--version 与 -V 等价");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_STR("版本输出", want, g_out);
    CC_CASE_END();
    case_cleanup();
}

/* ============================================================================
 * 成功解析（构建路径）
 * ============================================================================ */

static void test_build_min(void)
{
    const char *argv[] = { "ccmake", "-c", "a.ccm", "-f", "b.cmk" };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-001", "最小构建参数解析");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("stat", CC_CMD_FLAG_NEED_BUILD, ccmake_global_var.stat);
    CC_CHECK_EQ_STR("ccm_path", "a.ccm", ccmake_global_var.ccm_path);
    CC_CHECK_EQ_STR("cmk_path", "b.cmk", ccmake_global_var.cmk_path);
    CC_CHECK_EQ_STR("self_path", "ccmake", ccmake_global_var.self_path);
    CC_CHECK(ccmake_global_var.work_dir == NULL, "未设置 work_dir");
    CC_CHECK(ccmake_global_var.output_dir == NULL, "未设置 output_dir");
    CC_CHECK(ccmake_global_var.jobs > 0, "jobs 已解析为具体值");
    CC_CHECK_EQ_I64("action", CC_CMD_ACTION_NONE, ccmake_global_var.action);
    CC_CHECK_EQ_I64("verbose", CCFALSE, ccmake_global_var.verbose);
    CC_CHECK_EQ_I64("dry_run", CCFALSE, ccmake_global_var.dry_run);
    CC_CHECK_EQ_I64("always_build", CCFALSE, ccmake_global_var.always_build);
    CC_CHECK_EQ_I64("keep_going", CCFALSE, ccmake_global_var.keep_going);
    CC_CHECK(
        jobs_hint_matches("并行任务数：未指定，采用默认值 ", NULL),
        "未指定 -j 时输出默认值提示"
    );
    CC_CASE_END();
    case_cleanup();
}

static void test_build_jobs(void)
{
    const char *argv[] = { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "4" };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-002", "-j 指定并行任务数");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("jobs", 4, ccmake_global_var.jobs);
    CC_CHECK_EQ_STR("无默认值提示", "", g_out);
    CC_CASE_END();
    case_cleanup();
}

static void test_build_jobs_zero(void)
{
    const char *argv[] = { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "0" };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-003", "-j 0 自动检测并提示默认值");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK(ccmake_global_var.jobs > 0, "jobs 为自动检测结果");
    CC_CHECK(
        jobs_hint_matches("并行任务数：采用默认值 ", "（0 = 自动检测）\n"),
        "-j 0 输出自动检测提示"
    );
    CC_CASE_END();
    case_cleanup();
}

static void test_build_flags(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "2",
        "-v", "-n", "-B", "-k"
    };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-004", "-v -n -B -k 写入标志位");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("verbose", CCTRUE, ccmake_global_var.verbose);
    CC_CHECK_EQ_I64("dry_run", CCTRUE, ccmake_global_var.dry_run);
    CC_CHECK_EQ_I64("always_build", CCTRUE, ccmake_global_var.always_build);
    CC_CHECK_EQ_I64("keep_going", CCTRUE, ccmake_global_var.keep_going);
    CC_CHECK_EQ_STR("无默认值提示", "", g_out);
    CC_CASE_END();
    case_cleanup();
}

static void test_build_paths(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "2",
        "-C", "work", "-o", "outdir"
    };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-005", "-C -o 写入路径字段");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_STR("work_dir", "work", ccmake_global_var.work_dir);
    CC_CHECK_EQ_STR("output_dir", "outdir", ccmake_global_var.output_dir);
    CC_CASE_END();
    case_cleanup();
}

static void test_build_long(void)
{
    const char *argv[] = {
        "ccmake", "--config", "a.ccm", "--file", "b.cmk", "--jobs", "2"
    };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-006", "长格式选项解析");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_STR("ccm_path", "a.ccm", ccmake_global_var.ccm_path);
    CC_CHECK_EQ_STR("cmk_path", "b.cmk", ccmake_global_var.cmk_path);
    CC_CHECK_EQ_I64("jobs", 2, ccmake_global_var.jobs);
    CC_CASE_END();
    case_cleanup();
}

static void test_build_long_flags(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a.ccm", "-f", "b.cmk",
        "--directory", "work", "--output", "outdir",
        "--jobs", "2", "--verbose", "--dry-run", "--always-build", "--keep-going"
    };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-008", "长格式的路径与标志选项");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_STR("work_dir", "work", ccmake_global_var.work_dir);
    CC_CHECK_EQ_STR("output_dir", "outdir", ccmake_global_var.output_dir);
    CC_CHECK_EQ_I64("jobs", 2, ccmake_global_var.jobs);
    CC_CHECK_EQ_I64("verbose", CCTRUE, ccmake_global_var.verbose);
    CC_CHECK_EQ_I64("dry_run", CCTRUE, ccmake_global_var.dry_run);
    CC_CHECK_EQ_I64("always_build", CCTRUE, ccmake_global_var.always_build);
    CC_CHECK_EQ_I64("keep_going", CCTRUE, ccmake_global_var.keep_going);
    CC_CASE_END();
    case_cleanup();
}

static void test_build_space_paths(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a b.ccm", "-f", "b c.cmk", "-j", "2"
    };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-009", "含空格的路径按单个参数保存");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_STR("ccm_path", "a b.ccm", ccmake_global_var.ccm_path);
    CC_CHECK_EQ_STR("cmk_path", "b c.cmk", ccmake_global_var.cmk_path);
    CC_CASE_END();
    case_cleanup();
}

static void test_override(void)
{
    const char *argv[] = {
        "ccmake",
        "-c", "a.ccm", "-c", "b.ccm",
        "-f", "x.cmk",
        "-C", "d1", "-C", "d2",
        "-o", "o1", "-o", "o2",
        "-j", "2", "-j", "3"
    };
    CCBOOL ret;

    CC_CASE("CMD-BUILD-007", "重复参数以后出现者为准");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_STR("ccm_path", "b.ccm", ccmake_global_var.ccm_path);
    CC_CHECK_EQ_STR("work_dir", "d2", ccmake_global_var.work_dir);
    CC_CHECK_EQ_STR("output_dir", "o2", ccmake_global_var.output_dir);
    CC_CHECK_EQ_I64("jobs", 3, ccmake_global_var.jobs);
    CC_CASE_END();
    case_cleanup();
}

/* ============================================================================
 * -D / --define
 * ============================================================================ */

static void test_define_pairs(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "2",
        "-D", "a=1", "b=2", "a=3"
    };
    const char *key = NULL;
    const char *value = NULL;
    CCBOOL ret;

    CC_CASE("CMD-DEFINE-001", "一个 -D 跟多个键值对，重复键以后者为准");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("键值对数量", 2, define_count());
    CC_CHECK(define_at(0, &key, &value), "读取第 0 项");
    CC_CHECK_EQ_STR("第 0 项键", "a", key);
    CC_CHECK_EQ_STR("第 0 项值", "3", value);
    CC_CHECK(define_at(1, &key, &value), "读取第 1 项");
    CC_CHECK_EQ_STR("第 1 项键", "b", key);
    CC_CHECK_EQ_STR("第 1 项值", "2", value);
    CC_CASE_END();
    case_cleanup();
}

static void test_define_multi(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "2",
        "-D", "a=1", "-D", "a=2", "-D", "b=3"
    };
    const char *key = NULL;
    const char *value = NULL;
    CCBOOL ret;

    CC_CASE("CMD-DEFINE-002", "多个 -D 的重复键以后者为准");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("键值对数量", 2, define_count());
    CC_CHECK(define_at(0, &key, &value), "读取第 0 项");
    CC_CHECK_EQ_STR("第 0 项键", "a", key);
    CC_CHECK_EQ_STR("第 0 项值", "2", value);
    CC_CHECK(define_at(1, &key, &value), "读取第 1 项");
    CC_CHECK_EQ_STR("第 1 项键", "b", key);
    CC_CHECK_EQ_STR("第 1 项值", "3", value);
    CC_CASE_END();
    case_cleanup();
}

static void test_define_long(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "2",
        "--define", "a=1", "b=2"
    };
    const char *key = NULL;
    const char *value = NULL;
    CCBOOL ret;

    CC_CASE("CMD-DEFINE-003", "--define 长格式");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("键值对数量", 2, define_count());
    CC_CHECK(define_at(0, &key, &value), "读取第 0 项");
    CC_CHECK_EQ_STR("第 0 项键", "a", key);
    CC_CHECK_EQ_STR("第 0 项值", "1", value);
    CC_CASE_END();
    case_cleanup();
}

static void test_define_value_equal(void)
{
    const char *argv[] = {
        "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "2",
        "-D", "a=b=c"
    };
    const char *key = NULL;
    const char *value = NULL;
    CCBOOL ret;

    CC_CASE("CMD-DEFINE-004", "值中的等号保留");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("键值对数量", 1, define_count());
    CC_CHECK(define_at(0, &key, &value), "读取第 0 项");
    CC_CHECK_EQ_STR("第 0 项键", "a", key);
    CC_CHECK_EQ_STR("第 0 项值", "b=c", value);
    CC_CASE_END();
    case_cleanup();
}

/* ============================================================================
 * 需要配置的动作类参数
 * ============================================================================ */

static void test_action_dump(void)
{
    const char *argv[] = {
        "ccmake", "--dump-config", "-c", "a.ccm", "-f", "b.cmk"
    };
    CCBOOL ret;

    CC_CASE("CMD-ACTION-002", "--dump-config 记录动作请求并继续收集配置");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("action", CC_CMD_ACTION_DUMP_CONFIG, ccmake_global_var.action);
    CC_CHECK_EQ_STR("ccm_path", "a.ccm", ccmake_global_var.ccm_path);
    CC_CHECK_EQ_STR("cmk_path", "b.cmk", ccmake_global_var.cmk_path);
    CC_CHECK_EQ_I64("jobs", 0, ccmake_global_var.jobs);
    CC_CHECK_EQ_STR("解析阶段无输出", "", g_out);
    CC_CASE_END();
    case_cleanup();
}

/* ============================================================================
 * 动作与错误的优先级
 * ============================================================================ */

static void test_priority_action_first(void)
{
    const char *argv[] = { "ccmake", "-V", "-j" };
    char want[256];
    CCBOOL ret;

    make_version_text(want, sizeof(want));

    CC_CASE("CMD-PRIORITY-001", "动作先于后续缺值参数");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("stat", CC_CMD_FLAG_ACTION_DONE, ccmake_global_var.stat);
    CC_CHECK_EQ_STR("版本输出", want, g_out);
    CC_CASE_END();
    case_cleanup();
}

static void test_priority_error_first(void)
{
    const char *argv[] = { "ccmake", "-j", "-V" };
    CCBOOL ret;

    CC_CASE("CMD-PRIORITY-002", "缺值错误先于动作");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCFALSE, ret);
    CC_CHECK_EQ_STR("错误输出", "未找到jobs参数：-j <---\n", g_out);
    CC_CASE_END();
    case_cleanup();
}

static void test_order_help_first(void)
{
    const char *argv[] = { "ccmake", "-h", "-V" };
    CCBOOL ret;

    CC_CASE("CMD-ORDER-001", "-h 在前时输出帮助");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_I64("正文行数", 13, count_lines(g_out));
    CC_CHECK(!text_has(g_out, "版本：v"), "帮助输出不含版本段");
    CC_CASE_END();
    case_cleanup();
}

static void test_order_version_first(void)
{
    const char *argv[] = { "ccmake", "-V", "-h" };
    char want[256];
    CCBOOL ret;

    make_version_text(want, sizeof(want));

    CC_CASE("CMD-ORDER-002", "-V 在前时输出版本");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCTRUE, ret);
    CC_CHECK_EQ_STR("版本输出", want, g_out);
    CC_CASE_END();
    case_cleanup();
}

/* ============================================================================
 * 未知参数
 * ============================================================================ */

static void test_unknown(void)
{
    const char *argv[] = { "ccmake", "-z" };
    CCBOOL ret;

    CC_CASE("CMD-UNKNOWN-001", "未知选项报错并输出帮助");
    ret = run_parse(ARGC(argv), argv);
    CC_CHECK_EQ_I64("返回值", CCFALSE, ret);
    CC_CHECK_EQ_I64("正文行数", 14, count_lines(g_out));
    CC_CHECK(text_has(g_out, "未知的指令参数：-z\n"), "包含未知参数提示");
    CC_CHECK(text_has(g_out, "-c, --config"), "包含帮助内容");
    CC_CASE_END();
    case_cleanup();
}

/* ============================================================================
 * 缺值、空值、非法值与位置参数
 * ============================================================================ */

typedef struct msg_case {
    const char *id;
    const char *name;
    const char *argv[16];
    const char *want_out;
} msg_case;

static const msg_case g_msg_cases[] = {
    { "CMD-MSG-001", "缺少 -c/--config",
      { "ccmake", "-f", "b.cmk", NULL },
      "缺少必填参数：-c/--config\n" },
    { "CMD-MSG-002", "缺少 -f/--file",
      { "ccmake", "-c", "a.ccm", NULL },
      "缺少必填参数：-f/--file\n" },
    { "CMD-MSG-003", "-c 缺值",
      { "ccmake", "-c", NULL },
      "未找到config参数：-c <---\n" },
    { "CMD-MSG-004", "-c 空字符串",
      { "ccmake", "-c", "", "-f", "b.cmk", NULL },
      "-c 的参数为空字符串 <---\n" },
    { "CMD-MSG-005", "-f 缺值",
      { "ccmake", "-c", "a.ccm", "-f", NULL },
      "未找到file参数：-f <---\n" },
    { "CMD-MSG-006", "-f 空字符串",
      { "ccmake", "-c", "a.ccm", "-f", "", NULL },
      "-f 的参数为空字符串 <---\n" },
    { "CMD-MSG-007", "-C 缺值",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-C", NULL },
      "未找到directory参数：-C <---\n" },
    { "CMD-MSG-008", "-C 空字符串",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-C", "", NULL },
      "-C 的参数为空字符串 <---\n" },
    { "CMD-MSG-009", "-o 缺值",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-o", NULL },
      "未找到output参数：-o <---\n" },
    { "CMD-MSG-010", "-o 空字符串",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-o", "", NULL },
      "-o 的参数为空字符串 <---\n" },
    { "CMD-MSG-013", "-j 缺值",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", NULL },
      "未找到jobs参数：-j <---\n" },
    { "CMD-MSG-014", "-j 空字符串",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "", NULL },
      "-j 的参数为空字符串 <---\n" },
    { "CMD-MSG-015", "-j 非数字",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "abc", NULL },
      "jobs参数非法：abc（应为非负整数）\n" },
    { "CMD-MSG-016", "-j 负数被当作缺值",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "-1", NULL },
      "未找到jobs参数：-j <---\n" },
    { "CMD-MSG-017", "-j 溢出",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-j", "2147483648", NULL },
      "jobs参数非法：2147483648（应为非负整数）\n" },
    { "CMD-MSG-018", "-D 缺值",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-D", NULL },
      "未找到define参数：-D <---\n" },
    { "CMD-MSG-019", "-D 缺少等号",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-D", "a", NULL },
      "无法解析的键值对：a（应为 <键>=<值>，键值均不能为空）\n" },
    { "CMD-MSG-020", "-D 空键",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-D", "=b", NULL },
      "无法解析的键值对：=b（应为 <键>=<值>，键值均不能为空）\n" },
    { "CMD-MSG-021", "-D 空值",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-D", "a=", NULL },
      "无法解析的键值对：a=（应为 <键>=<值>，键值均不能为空）\n" },
    { "CMD-MSG-022", "-D 空字符串参数",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "-D", "", NULL },
      "define 的参数为空字符串 <---\n" },
    { "CMD-MSG-023", "位置参数不被接受",
      { "ccmake", "-c", "a.ccm", "-f", "b.cmk", "release", NULL },
      "不合规的参数：release <---\n" }
};

static int argv_count(const char *const argv[])
{
    int count = 0;
    while (argv[count])
        count++;
    return count;
}

static void test_msg_cases(void)
{
    CCUINT32 i;

    for (i = 0; i < (CCUINT32)(sizeof(g_msg_cases) / sizeof(g_msg_cases[0])); i++)
    {
        const msg_case *item = &g_msg_cases[i];
        CCBOOL ret;

        CC_CASE(item->id, item->name);
        ret = run_parse(argv_count(item->argv), item->argv);
        CC_CHECK_EQ_I64("返回值", CCFALSE, ret);
        CC_CHECK_EQ_STR("输出", item->want_out, g_out);
        CC_CHECK_EQ_I64("正文行数", 1, count_lines(g_out));
        CC_CASE_END();
        case_cleanup();
    }
}

/* ============================================================================
 * 套件入口
 * ============================================================================ */

void test_cmd(void)
{
    CC_CASE("CMD-HARNESS-000", "stdout 捕获自检");
    if (!capture_selftest())
    {
        CC_CHECK(CCFALSE, "stdout 捕获不可用（见 stderr 诊断）");
        CC_CASE_END();
        return;
    }
    CC_CHECK(CCTRUE, "stdout 捕获可用");
    CC_CASE_END();

    test_no_args();
    test_help_short();
    test_help_long();
    test_version_short();
    test_version_long();
    test_build_min();
    test_build_jobs();
    test_build_jobs_zero();
    test_build_flags();
    test_build_paths();
    test_build_long();
    test_build_long_flags();
    test_build_space_paths();
    test_override();
    test_define_pairs();
    test_define_multi();
    test_define_long();
    test_define_value_equal();
    test_action_dump();
    test_priority_action_first();
    test_priority_error_first();
    test_order_help_first();
    test_order_version_first();
    test_unknown();
    test_msg_cases();
}
