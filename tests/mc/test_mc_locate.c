/*
 * @Description: MC 文件检查与定位测试。直接调用 mc_locate，校验文件表、
 *               include 边、解析顺序、错误提示与 VFS 清理。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "mc/mc_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define OUT_CAP 8192

static char g_out[OUT_CAP];

/* ============================================================================
 * 辅助
 * ============================================================================ */

static CCUINT32 files_count(const CCMakeMCContext *ctx)
{
    return ctx->files.size / (CCUINT32)sizeof(CCMakeMCFile);
}

static CCUINT32 edges_count(const CCMakeMCContext *ctx)
{
    return ctx->includes.size / (CCUINT32)sizeof(CCMakeMCInclude);
}

static CCMakeMCFile *file_at(CCMakeMCContext *ctx, CCUINT32 index)
{
    return &((CCMakeMCFile *)ctx->files.array)[index];
}

static CCMakeMCInclude *edge_at(CCMakeMCContext *ctx, CCUINT32 index)
{
    return &((CCMakeMCInclude *)ctx->includes.array)[index];
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static CCBOOL order_basename_is(CCMakeMCContext *ctx, CCUINT32 pos, const char *expected)
{
    CCUINT32 index = 0;
    if (cc_dyn_core_seek(
            &ctx->order,
            (CCUINT32)sizeof(CCUINT32),
            (CCINT32)sizeof(CCUINT32),
            &index, pos) != 0)
        return CCFALSE;
    if (index >= files_count(ctx))
        return CCFALSE;
    return (strcmp(path_basename(file_at(ctx, index)->real_path), expected) == 0)
               ? CCTRUE : CCFALSE;
}

static CCBOOL text_has(const char *needle)
{
    return (strstr(g_out, needle) != NULL) ? CCTRUE : CCFALSE;
}

/* 定位一个文件并捕获阶段一输出 */
static CCBOOL run_locate(
    const char *root,
    const char *work_dir,
    CCMakeMCContext *ctx
)
{
    cc_test_capture cap;

    g_out[0] = '\0';
    if (!cc_test_capture_begin(&cap))
        return mc_locate(root, work_dir, ctx);

    CCBOOL ok = mc_locate(root, work_dir, ctx);
    cc_test_capture_end(&cap, g_out, sizeof(g_out));
    return ok;
}

/* 阶段一结束后 VFS 中不应残留任何合成节点 */
static CCBOOL vfs_is_clean(CCMakeMCContext *ctx)
{
    CCFiles *fs = ccfiles_create();
    if (!fs)
        return CCFALSE;

    CCBOOL clean = CCTRUE;
    CCUINT32 count = files_count(ctx);
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCMakeMCFile *f = file_at(ctx, i);
        if (!f->v_path)
            continue;
        CCFsStat st;
        if (ccfiles_stat(fs, f->v_path, &st) != CC_FS_ERR_NOENT)
            clean = CCFALSE;
    }
    ccfiles_destroy(fs);
    return clean;
}

static CCBOOL write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return CCFALSE;
    size_t len = strlen(text);
    size_t wrote = fwrite(text, 1, len, fp);
    int closed = fclose(fp);
    return (wrote == len && closed == 0) ? CCTRUE : CCFALSE;
}

/* 在 out/ 下生成一份“绝对路径 include”的临时 fixture，返回根文件绝对路径 */
static CCBOOL make_absolute_fixture(char *root_out, size_t cap)
{
    char *cwd = mc_path_cwd(NULL);
    if (!cwd)
        return CCFALSE;

    char *dir = mc_path_normalize("out/mc_test_abs", cwd, NULL);
    ccalloc(cwd, 0, NULL);
    if (!dir)
        return CCFALSE;

#ifdef CC_WINDOWS
    int made = _mkdir(dir);
#else
    int made = mkdir(dir, 0777);
#endif
    if (made != 0 && errno != EEXIST)
    {
        ccalloc(dir, 0, NULL);
        return CCFALSE;
    }

    char *base = mc_path_normalize("base.mc", dir, NULL);
    char *root = mc_path_normalize("root.mc", dir, NULL);
    if (!base || !root)
    {
        if (base) ccalloc(base, 0, NULL);
        if (root) ccalloc(root, 0, NULL);
        ccalloc(dir, 0, NULL);
        return CCFALSE;
    }

    CCBOOL ok = write_text_file(base, "\"base\": 1;\n");
    char content[2048];
    snprintf(content, sizeof(content),
             "@include \"%s\";\n\"root\": true;\n", base);
    if (ok)
        ok = write_text_file(root, content);

    if (ok)
        snprintf(root_out, cap, "%s", root);

    ccalloc(base, 0, NULL);
    ccalloc(root, 0, NULL);
    ccalloc(dir, 0, NULL);
    return ok;
}

/* ============================================================================
 * 用例
 * ============================================================================ */

static void test_single_file(void)
{
    CC_CASE("MC-001", "单文件定位与内容缓存");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(run_locate("tests/data/mc/single/root.mc", NULL, &ctx), "定位成功");
    CC_CHECK_EQ_I64("文件数", 1, files_count(&ctx));
    CC_CHECK_EQ_I64("include 边数", 0, edges_count(&ctx));
    CC_CHECK_EQ_I64("解析顺序数量", 1, ctx.order.size / sizeof(CCUINT32));
    CC_CHECK(order_basename_is(&ctx, 0, "root.mc"), "解析顺序为根文件");
    CC_CHECK(file_at(&ctx, 0)->content != NULL, "内容已缓存");
    CC_CHECK_EQ_I64("内容字节数", file_at(&ctx, 0)->size,
                    (CCUINT64)strlen((const char *)file_at(&ctx, 0)->content));
    CC_CHECK(strstr(file_at(&ctx, 0)->content, "single") != NULL, "内容正确");
    CC_CHECK(vfs_is_clean(&ctx), "VFS 无残留节点");

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_chain(void)
{
    CC_CASE("MC-002", "链式 include 的后序顺序");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(run_locate("tests/data/mc/chain/root.mc", NULL, &ctx), "定位成功");
    CC_CHECK_EQ_I64("文件数", 3, files_count(&ctx));
    CC_CHECK_EQ_I64("include 边数", 2, edges_count(&ctx));
    CC_CHECK(order_basename_is(&ctx, 0, "b.mc"), "顺序 0 = b");
    CC_CHECK(order_basename_is(&ctx, 1, "a.mc"), "顺序 1 = a");
    CC_CHECK(order_basename_is(&ctx, 2, "root.mc"), "顺序 2 = root");
    CC_CHECK_EQ_I64("b 的父文件", 1, file_at(&ctx, 2)->parent);
    CC_CHECK_EQ_I64("a 的行号", 1, edge_at(&ctx, 0)->line);
    CC_CHECK(vfs_is_clean(&ctx), "VFS 无残留节点");

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_diamond(void)
{
    CC_CASE("MC-003", "菱形依赖只解析一次");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(run_locate("tests/data/mc/diamond/root.mc", NULL, &ctx), "定位成功");
    CC_CHECK_EQ_I64("文件数", 4, files_count(&ctx));
    CC_CHECK_EQ_I64("include 边数", 4, edges_count(&ctx));
    CC_CHECK(order_basename_is(&ctx, 0, "base.mc"), "顺序 0 = base");
    CC_CHECK(order_basename_is(&ctx, 1, "a.mc"), "顺序 1 = a");
    CC_CHECK(order_basename_is(&ctx, 2, "b.mc"), "顺序 2 = b");
    CC_CHECK(order_basename_is(&ctx, 3, "root.mc"), "顺序 3 = root");
    CC_CHECK_EQ_I64("a 与 b 共享 base", edge_at(&ctx, 2)->child, edge_at(&ctx, 3)->child);
    CC_CHECK(vfs_is_clean(&ctx), "VFS 无残留节点");

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_duplicate(void)
{
    CC_CASE("MC-004", "重复 include 只保留首次");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(run_locate("tests/data/mc/duplicate/root.mc", NULL, &ctx), "定位成功");
    CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));
    CC_CHECK_EQ_I64("include 边数", 3, edges_count(&ctx));
    CC_CHECK_EQ_I64("边 0 指向 base", 1, edge_at(&ctx, 0)->child);
    CC_CHECK_EQ_I64("边 1 指向 base", 1, edge_at(&ctx, 1)->child);
    CC_CHECK_EQ_I64("边 2 指向 base", 1, edge_at(&ctx, 2)->child);
    CC_CHECK(order_basename_is(&ctx, 0, "base.mc"), "顺序 0 = base");
    CC_CHECK(order_basename_is(&ctx, 1, "root.mc"), "顺序 1 = root");

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_cycle(void)
{
    CC_CASE("MC-005", "间接循环导入报错");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(!run_locate("tests/data/mc/cycle/root.mc", NULL, &ctx), "定位失败");
    CC_CHECK(text_has("循环导入"), "提示循环导入");
    CC_CHECK(text_has("a.mc") && text_has("b.mc"), "提示循环链");
    CC_CHECK(vfs_is_clean(&ctx), "失败后 VFS 干净");

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_self_cycle(void)
{
    CC_CASE("MC-006", "自包含报错");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(!run_locate("tests/data/mc/cycle/self.mc", NULL, &ctx), "定位失败");
    CC_CHECK(text_has("循环导入"), "提示循环导入");

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_comments_and_strings(void)
{
    CC_CASE("MC-007", "注释与字符串中的 @include 不计");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(run_locate("tests/data/mc/comments/root.mc", NULL, &ctx), "定位成功");
    CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));
    CC_CHECK_EQ_I64("include 边数", 1, edges_count(&ctx));
    CC_CHECK_EQ_I64("边行号", 7, edge_at(&ctx, 0)->line);
    CC_CHECK(order_basename_is(&ctx, 0, "a.mc"), "顺序 0 = a");
    CC_CHECK(g_out[0] == '\0', "成功时无输出");

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_bad_directives(void)
{
    CC_CASE("MC-008", "非法 @include 指令报错");
    CCMakeMCContext ctx;

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/bad/nested.mc", NULL, &ctx), "非顶层失败");
    CC_CHECK(text_has("顶层"), "提示顶层限制");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/bad/no_quote.mc", NULL, &ctx), "缺双引号失败");
    CC_CHECK(text_has("双引号"), "提示双引号");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/bad/no_semi.mc", NULL, &ctx), "缺分号失败");
    CC_CHECK(text_has("分号"), "提示分号");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/bad/unterminated.mc", NULL, &ctx), "未闭合字符串失败");
    CC_CHECK(text_has("字符串未闭合"), "提示字符串未闭合");
    mc_context_clear(&ctx);

    CC_CASE_END();
}

static void test_missing_and_suffix(void)
{
    CC_CASE("MC-009", "缺失目标与任意后缀");
    CCMakeMCContext ctx;

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/bad/missing.mc", NULL, &ctx), "缺失 include 失败");
    CC_CHECK(text_has("文件不存在"), "提示文件不存在");
    CC_CHECK(vfs_is_clean(&ctx), "失败后 VFS 干净");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(run_locate("tests/data/mc/bad/bad_ext.mc", NULL, &ctx),
             "任意后缀 include 成功");
    CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));
    CC_CHECK(order_basename_is(&ctx, 0, "base.txt"), "base.txt 先解析");
    CC_CHECK(order_basename_is(&ctx, 1, "bad_ext.mc"), "根文件最后");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(run_locate("tests/data/mc/single/root_no_ext", NULL, &ctx),
             "无后缀根文件成功");
    CC_CHECK_EQ_I64("文件数", 1, files_count(&ctx));
    CC_CHECK(order_basename_is(&ctx, 0, "root_no_ext"), "根文件入顺序");
    mc_context_clear(&ctx);

    CC_CASE_END();
}

static void test_root_errors(void)
{
    CC_CASE("MC-010", "根文件缺失、目录与空路径");
    CCMakeMCContext ctx;

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/no_such_root.mc", NULL, &ctx), "缺失根失败");
    CC_CHECK(text_has("文件不存在"), "提示文件不存在");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/bad/dir.mc", NULL, &ctx), "目录根失败");
    CC_CHECK(text_has("目录"), "提示目标是目录");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate(NULL, NULL, &ctx), "空根路径失败");
    CC_CHECK(text_has("缺少 -c"), "提示缺少 -c");
    mc_context_clear(&ctx);

    CC_CASE_END();
}

static void test_encoding(void)
{
    CC_CASE("MC-011", "BOM、CRLF、空文件与非法编码");
    CCMakeMCContext ctx;

    mc_context_init(&ctx);
    CC_CHECK(run_locate("tests/data/mc/encoding/empty.mc", NULL, &ctx), "空文件成功");
    CC_CHECK_EQ_I64("空文件数", 1, files_count(&ctx));
    CC_CHECK_EQ_I64("空文件大小", 0, file_at(&ctx, 0)->size);
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(run_locate("tests/data/mc/encoding/bom.mc", NULL, &ctx), "BOM 文件成功");
    CC_CHECK_EQ_I64("BOM 文件数", 2, files_count(&ctx));
    CC_CHECK_EQ_I64("BOM 已剥离", '@', (unsigned char)file_at(&ctx, 0)->content[0]);
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(run_locate("tests/data/mc/encoding/crlf.mc", NULL, &ctx), "CRLF 文件成功");
    CC_CHECK_EQ_I64("CRLF 文件数", 2, files_count(&ctx));
    CC_CHECK_EQ_I64("CRLF 行号", 1, edge_at(&ctx, 0)->line);
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/encoding/nul_byte.mc", NULL, &ctx), "NUL 文件失败");
    CC_CHECK(text_has("NUL"), "提示 NUL");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("tests/data/mc/encoding/utf16.mc", NULL, &ctx), "UTF-16 文件失败");
    CC_CHECK(text_has("UTF-16"), "提示 UTF-16");
    mc_context_clear(&ctx);

    CC_CASE_END();
}

static void test_work_dir(void)
{
    CC_CASE("MC-012", "-C 工作目录参与根路径解析");
    CCMakeMCContext ctx;

    mc_context_init(&ctx);
    CC_CHECK(run_locate("root.mc", "tests/data/mc/workdir/project", &ctx), "工作目录相对根成功");
    CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));
    CC_CHECK(order_basename_is(&ctx, 0, "base.mc"), "顺序 0 = base");
    CC_CHECK(order_basename_is(&ctx, 1, "root.mc"), "顺序 1 = root");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(run_locate("project/root.mc", "tests/data/mc/workdir", &ctx), "子目录根成功");
    CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(!run_locate("root.mc", "tests/data/mc/no_such_dir", &ctx), "工作目录缺失失败");
    CC_CHECK(text_has("工作目录"), "提示工作目录");
    mc_context_clear(&ctx);

    CC_CASE_END();
}

static void test_absolute_include(void)
{
    CC_CASE("MC-013", "绝对路径 include");
    char root[1024];
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    if (!make_absolute_fixture(root, sizeof(root)))
    {
        CC_CHECK(CCFALSE, "生成绝对路径 fixture");
    }
    else
    {
        CC_CHECK(run_locate(root, NULL, &ctx), "绝对路径 include 成功");
        CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));
        CC_CHECK(order_basename_is(&ctx, 0, "base.mc"), "顺序 0 = base");
        CC_CHECK(order_basename_is(&ctx, 1, "root.mc"), "顺序 1 = root");
        CC_CHECK(mc_path_is_absolute(file_at(&ctx, 0)->real_path), "include 路径为绝对路径");
    }

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_space_path(void)
{
    CC_CASE("MC-014", "包含空格的文件路径");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(run_locate("tests/data/mc/space dir/root.mc", NULL, &ctx), "空格路径成功");
    CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));

    mc_context_clear(&ctx);
    CC_CASE_END();
}

static void test_case_dedup(void)
{
    CC_CASE("MC-015", "Windows 大小写路径去重");

#ifdef CC_WINDOWS
    CCMakeMCContext ctx;
    mc_context_init(&ctx);
    CC_CHECK(run_locate("tests/data/mc/case/root.mc", NULL, &ctx), "大小写路径成功");
    CC_CHECK_EQ_I64("文件数", 2, files_count(&ctx));
    CC_CHECK_EQ_I64("两条边指向同一文件", edge_at(&ctx, 0)->child, edge_at(&ctx, 1)->child);
    mc_context_clear(&ctx);
#else
    CC_CHECK(CCTRUE, "非 Windows 平台跳过大小写去重");
#endif

    CC_CASE_END();
}

static void test_repeat_locate(void)
{
    CC_CASE("MC-016", "连续定位与资源清理");
    CCMakeMCContext ctx;
    mc_context_init(&ctx);

    CC_CHECK(run_locate("tests/data/mc/chain/root.mc", NULL, &ctx), "第一次定位成功");
    CC_CHECK(vfs_is_clean(&ctx), "第一次定位后 VFS 干净");
    mc_context_clear(&ctx);

    mc_context_init(&ctx);
    CC_CHECK(run_locate("tests/data/mc/chain/root.mc", NULL, &ctx), "第二次定位成功");
    CC_CHECK_EQ_I64("文件数", 3, files_count(&ctx));
    CC_CHECK(vfs_is_clean(&ctx), "第二次定位后 VFS 干净");
    mc_context_clear(&ctx);

    CC_CASE_END();
}

static void test_path_normalize(void)
{
    CC_CASE("MC-017", "路径规范化与目录提取");

    char *p = mc_path_normalize("a/./b/../c", "/base/dir", NULL);
    CC_CHECK_EQ_STR("相对路径归一化", "/base/dir/a/c", p);
    if (p) ccalloc(p, 0, NULL);

    p = mc_path_normalize("../../x", "/a/b", NULL);
    CC_CHECK_EQ_STR(".. 越过根", "/x", p);
    if (p) ccalloc(p, 0, NULL);

    p = mc_path_normalize("/a//b/", NULL, NULL);
    CC_CHECK_EQ_STR("重复与末尾斜杠", "/a/b", p);
    if (p) ccalloc(p, 0, NULL);

    p = mc_path_normalize("/a/b/../../..", NULL, NULL);
    CC_CHECK_EQ_STR("根处 .. 钳制", "/", p);
    if (p) ccalloc(p, 0, NULL);

    p = mc_path_dirname("/a/b/c.mc", NULL);
    CC_CHECK_EQ_STR("目录提取", "/a/b", p);
    if (p) ccalloc(p, 0, NULL);

    p = mc_path_dirname("/x.mc", NULL);
    CC_CHECK_EQ_STR("根目录提取", "/", p);
    if (p) ccalloc(p, 0, NULL);

#ifdef CC_WINDOWS
    p = mc_path_normalize("c:/A/../b", NULL, NULL);
    CC_CHECK_EQ_STR("Windows 盘符路径", "C:/b", p);
    if (p) ccalloc(p, 0, NULL);

    p = mc_path_normalize("//server/share/a/../b", NULL, NULL);
    CC_CHECK_EQ_STR("Windows UNC 路径", "//server/share/b", p);
    if (p) ccalloc(p, 0, NULL);
#endif

    CC_CHECK(mc_path_is_absolute("/x"), "POSIX 绝对路径");
    CC_CHECK(!mc_path_is_absolute("x/y"), "相对路径");

    CC_CASE_END();
}

void test_mc_locate(void)
{
    test_single_file();
    test_chain();
    test_diamond();
    test_duplicate();
    test_cycle();
    test_self_cycle();
    test_comments_and_strings();
    test_bad_directives();
    test_missing_and_suffix();
    test_root_errors();
    test_encoding();
    test_work_dir();
    test_absolute_include();
    test_space_path();
    test_case_dedup();
    test_repeat_locate();
    test_path_normalize();
}
