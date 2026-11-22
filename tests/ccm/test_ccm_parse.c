/*
 * @Description: CCM 第二阶段解析测试：类型、顺序、合并、内建函数与错误。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "ccm/ccm_internal.h"
#include "ccm/ccm_safe.h"
#include "ccvar.h"

#include <stdio.h>
#include <string.h>

#define OUT_CAP 8192
static char g_out[OUT_CAP];

static CCBOOL value_string_is(const CCVarValue *v, const char *expect)
{
    if (v->type != CC_VAR_STRING)
        return CCFALSE;
    size_t len = strlen(expect);
    return (v->as.string.len == len &&
            memcmp(v->as.string.data, expect, len) == 0) ? CCTRUE : CCFALSE;
}

static void defines_reset(void)
{
    CCMakeDefine def;
    while (cc_dyn_core_pop(&ccmake_global_var.defines,
                           (CCUINT32)sizeof(CCMakeDefine),
                           (CCINT32)sizeof(CCMakeDefine), &def) == 0)
    {
        if (def.key) ccalloc(def.key, 0, NULL);
        if (def.value) ccalloc(def.value, 0, NULL);
    }
    if (ccmake_global_var.defines.array)
        ccalloc(ccmake_global_var.defines.array, 0, NULL);
    memset(&ccmake_global_var.defines, 0, sizeof(ccmake_global_var.defines));
}

static CCBOOL define_add(const char *key, const char *value)
{
    CCMakeDefine def;
    memset(&def, 0, sizeof(def));
    size_t kl = strlen(key);
    size_t vl = strlen(value);
    def.key = ccalloc(NULL, kl + 1, CC_DBG_REL_TAG("测试 -D 键", NULL));
    def.value = ccalloc(NULL, vl + 1, CC_DBG_REL_TAG("测试 -D 值", NULL));
    if (!def.key || !def.value)
    {
        if (def.key) ccalloc(def.key, 0, NULL);
        if (def.value) ccalloc(def.value, 0, NULL);
        return CCFALSE;
    }
    memcpy(def.key, key, kl + 1);
    memcpy(def.value, value, vl + 1);
    if (cc_dyn_core_push(&ccmake_global_var.defines,
                         (CCUINT32)sizeof(CCMakeDefine),
                         (CCINT32)sizeof(CCMakeDefine), &def) != 0)
    {
        ccalloc(def.key, 0, NULL);
        ccalloc(def.value, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL parse_run_limits(
    const char *root,
    CCMakeCCMContext *ctx,
    CCUINT32 max_token,
    CCUINT32 max_array,
    CCUINT32 max_object,
    CCUINT32 max_depth
)
{
    ccm_context_init(ctx);
    if (max_token)
        ctx->limits.max_token_bytes = max_token;
    if (max_array)
        ctx->limits.max_array_elements = max_array;
    if (max_object)
        ctx->limits.max_object_members = max_object;
    if (max_depth)
        ctx->limits.max_value_depth = max_depth;
    if (!ccm_locate(root, NULL, ctx))
        return CCFALSE;
    if (!ccvar_global_init())
        return CCFALSE;
    if (ccvar_clear(ccvar_global()) != CC_VAR_OK)
        return CCFALSE;
    return ccm_parse(ctx);
}

static CCBOOL parse_run(const char *root, CCMakeCCMContext *ctx)
{
    return parse_run_limits(root, ctx, 0, 0, 0, 0);
}

static CCBOOL parse_run_include_limits(
    const char *root,
    CCMakeCCMContext *ctx,
    CCUINT32 max_files,
    CCUINT32 max_edges,
    CCUINT32 max_depth
)
{
    ccm_context_init(ctx);
    if (max_files)
        ctx->limits.max_files = max_files;
    if (max_edges)
        ctx->limits.max_include_edges = max_edges;
    if (max_depth)
        ctx->limits.max_include_depth = max_depth;
    if (!ccm_locate(root, NULL, ctx))
        return CCFALSE;
    if (!ccvar_global_init())
        return CCFALSE;
    if (ccvar_clear(ccvar_global()) != CC_VAR_OK)
        return CCFALSE;
    return ccm_parse(ctx);
}

static CCBOOL parse_run_find_limit(
    const char *root,
    CCMakeCCMContext *ctx,
    CCUINT32 max_results
)
{
    ccm_context_init(ctx);
    ctx->limits.max_find_results = max_results;
    if (!ccm_locate(root, NULL, ctx))
        return CCFALSE;
    if (!ccvar_global_init())
        return CCFALSE;
    if (ccvar_clear(ccvar_global()) != CC_VAR_OK)
        return CCFALSE;
    return ccm_parse(ctx);
}

static CCBOOL parse_capture(const char *root, CCMakeCCMContext *ctx)
{
    cc_test_capture cap;
    g_out[0] = '\0';
    if (!cc_test_capture_begin(&cap))
        return parse_run(root, ctx);
    CCBOOL ok = parse_run(root, ctx);
    cc_test_capture_end(&cap, g_out, sizeof(g_out));
    return ok;
}

static void parse_cleanup(CCMakeCCMContext *ctx)
{
    ccm_context_clear(ctx);
    ccvar_global_deinit();
    defines_reset();
}

static void test_basic(void)
{
    CC_CASE("PARSE-001", "基本类型与嵌套对象");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/basic.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("字符串", CC_VAR_OK, ccvar_get(ccvar_global(), "project", &v));
    CC_CHECK(value_string_is(&v, "demo"), "字符串值");
    CC_CHECK_EQ_I64("整数", CC_VAR_OK, ccvar_get(ccvar_global(), "port", &v));
    CC_CHECK_EQ_I64("整数值", 8080, v.as.integer);
    CC_CHECK_EQ_I64("布尔", CC_VAR_OK, ccvar_get(ccvar_global(), "debug", &v));
    CC_CHECK(v.as.boolean == CCTRUE, "布尔值");
    CC_CHECK_EQ_I64("浮点", CC_VAR_OK, ccvar_get(ccvar_global(), "ratio", &v));
    CC_CHECK(v.as.floating > 1.49 && v.as.floating < 1.51, "浮点值");
    CC_CHECK_EQ_I64("嵌套", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "toolchain.linker", &v));
    CC_CHECK(value_string_is(&v, "ld"), "嵌套值");
    CC_CHECK_EQ_I64("数组长度", 2, ccvar_array_size(ccvar_global(), "defines"));
    CC_CHECK_EQ_I64("数组 [1]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "defines[1]", &v));
    CC_CHECK(value_string_is(&v, "B"), "数组值");

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_order(void)
{
    CC_CASE("PARSE-002", "文件间顺序与根文件覆盖");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/order_root.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("x 最终值", CC_VAR_OK, ccvar_get(ccvar_global(), "x", &v));
    CC_CHECK_EQ_I64("x = 3", 3, v.as.integer);
    CC_CHECK_EQ_I64("a 的字段", CC_VAR_OK, ccvar_get(ccvar_global(), "only_a", &v));
    CC_CHECK_EQ_I64("b 的字段", CC_VAR_OK, ccvar_get(ccvar_global(), "only_b", &v));
    CC_CHECK_EQ_I64("根字段", CC_VAR_OK, ccvar_get(ccvar_global(), "only_root", &v));

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_merge(void)
{
    CC_CASE("PARSE-003", "对象浅合并");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/merge_root.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("覆写 c", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "toolchain.c", &v));
    CC_CHECK(value_string_is(&v, "clang"), "c=clang");
    CC_CHECK_EQ_I64("保留 linker", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "toolchain.linker", &v));
    CC_CHECK(value_string_is(&v, "ld"), "linker=ld");
    CC_CHECK_EQ_I64("嵌套对象替换", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "toolchain.flags.b", &v));
    CC_CHECK_EQ_I64("flags.b", 2, v.as.integer);
    CC_CHECK_EQ_I64("旧嵌套成员已删除", CC_VAR_NONE,
                    ccvar_type(ccvar_global(), "toolchain.flags.a"));

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_ref(void)
{
    CC_CASE("PARSE-004", "${ref} 字段引用");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/ref.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("引用顶层", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "copy", &v));
    CC_CHECK(value_string_is(&v, "./src"), "copy 值");
    CC_CHECK_EQ_I64("引用嵌套", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "copy_nested", &v));
    CC_CHECK(value_string_is(&v, "gcc"), "嵌套引用值");
    CC_CHECK_EQ_I64("引用数组", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "joined", &v));
    CC_CHECK(value_string_is(&v, "A B"), "数组连接值");

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_arrays(void)
{
    CC_CASE("PARSE-005", "嵌套数组与混合类型");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/arrays.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("矩阵 [0][1]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "matrix[0][1]", &v));
    CC_CHECK_EQ_I64("矩阵值", 2, v.as.integer);
    CC_CHECK_EQ_I64("矩阵 [1][0]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "matrix[1][0]", &v));
    CC_CHECK_EQ_I64("矩阵值", 3, v.as.integer);
    CC_CHECK_EQ_I64("mixed [1]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "mixed[1]", &v));
    CC_CHECK_EQ_I64("整数", 1, v.as.integer);
    CC_CHECK_EQ_I64("mixed [2]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "mixed[2]", &v));
    CC_CHECK(v.as.boolean == CCTRUE, "布尔");
    CC_CHECK_EQ_I64("mixed [3]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "mixed[3]", &v));
    CC_CHECK(v.as.floating > 1.49 && v.as.floating < 1.51, "浮点");
    CC_CHECK_EQ_I64("尾逗号数组", 2,
                    ccvar_array_size(ccvar_global(), "trailing"));
    CC_CHECK_EQ_I64("尾逗号 [1]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "trailing[1]", &v));
    CC_CHECK_EQ_I64("尾逗号值", 2, v.as.integer);

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_array_objects(void)
{
    CC_CASE("PARSE-006", "数组内的对象");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/array_objects.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("数组长度", 2, ccvar_array_size(ccvar_global(), "targets"));
    CC_CHECK_EQ_I64("第一个 name", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "targets[0].name", &v));
    CC_CHECK(value_string_is(&v, "app"), "数组对象字段");
    CC_CHECK_EQ_I64("第一个 sources", 2,
                    ccvar_array_size(ccvar_global(), "targets[0].sources"));
    CC_CHECK_EQ_I64("第二个 sources[0]", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "targets[1].sources[0]", &v));
    CC_CHECK(value_string_is(&v, "c.c"), "嵌套数组值");

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_dedupe(void)
{
    CC_CASE("PARSE-007", "同文件重复键后写覆盖");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/dedupe.ccm", &ctx), "解析成功");
    CCVarValue v;
    CC_CHECK_EQ_I64("读取 x", CC_VAR_OK, ccvar_get(ccvar_global(), "x", &v));
    CC_CHECK_EQ_I64("x = 2", 2, v.as.integer);
    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_empty(void)
{
    CC_CASE("PARSE-008", "空对象与空数组");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/empty.ccm", &ctx), "解析成功");
    CC_CHECK_EQ_I64("空对象子节点", 0, ccvar_child_count(ccvar_global(), "empty_object"));
    CC_CHECK_EQ_I64("空数组长度", 0, ccvar_array_size(ccvar_global(), "empty_array"));
    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_errors(void)
{
    CC_CASE("PARSE-009", "键名、值与引用错误");
    CCMakeCCMContext ctx;

    CC_CHECK(!parse_capture("tests/data/ccm/parse/bad_key.ccm", &ctx), "非法键失败");
    CC_CHECK(strstr(g_out, "键名非法") != NULL, "提示键名非法");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_capture("tests/data/ccm/parse/bad_value.ccm", &ctx), "非法值失败");
    CC_CHECK(strstr(g_out, "值格式非法") != NULL, "提示值格式非法");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_capture("tests/data/ccm/parse/ref_missing.ccm", &ctx), "引用缺失失败");
    CC_CHECK(strstr(g_out, "不存在") != NULL, "提示引用不存在");
    parse_cleanup(&ctx);

    CC_CASE_END();
}

static void test_find(void)
{
    CC_CASE("PARSE-010", "${find} 文件发现");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/find.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("读取 sources", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "sources", &v));
    CC_CHECK(v.type == CC_VAR_STRING, "结果为字符串");
    char text[4096];
    CCUINT32 n = v.as.string.len < sizeof(text) - 1 ? v.as.string.len : sizeof(text) - 1;
    memcpy(text, v.as.string.data, n);
    text[n] = '\0';
    CC_CHECK(strstr(text, "a.c") != NULL, "包含 a.c");
    CC_CHECK(strstr(text, "b.h") != NULL, "包含 b.h");
    CC_CHECK(strstr(text, "sub/c.c") != NULL, "包含 sub/c.c");
    CC_CHECK(strstr(text, "sub/d.h") != NULL, "包含 sub/d.h");
    CC_CHECK(strstr(text, "skip_test.c") == NULL, "排除测试文件");
    CC_CHECK(strstr(text, "lib/x.c") == NULL, "目录外文件不包含");

    CCFiles *fs = ccfiles_create();
    CC_CHECK(fs != NULL, "VFS 探针");
    if (fs)
    {
        CCFsStat st;
        CC_CHECK_EQ_I64("find 合成节点已清理", CC_FS_ERR_NOENT,
                        ccfiles_stat(fs, "/__find:0", &st));
        ccfiles_destroy(fs);
    }

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_defines(void)
{
    CC_CASE("PARSE-011", "-D 覆盖");
    defines_reset();
    CC_CHECK(define_add("project", "override"), "添加 -D");
    CC_CHECK(define_add("toolchain.c", "clang"), "添加 -D 路径");

    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/define.ccm", &ctx), "解析成功");
    CCVarValue v;
    CC_CHECK_EQ_I64("覆盖 project", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "project", &v));
    CC_CHECK(value_string_is(&v, "override"), "project 覆盖值");
    CC_CHECK_EQ_I64("覆盖嵌套", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "toolchain.c", &v));
    CC_CHECK(value_string_is(&v, "clang"), "toolchain.c 覆盖值");

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_define_bad_key(void)
{
    CC_CASE("PARSE-012", "-D 非法键");
    defines_reset();
    CC_CHECK(define_add("bad-key", "x"), "添加非法 -D");

    CCMakeCCMContext ctx;
    CC_CHECK(!parse_capture("tests/data/ccm/parse/define.ccm", &ctx), "解析失败");
    CC_CHECK(strstr(g_out, "-D 键名非法") != NULL, "提示 -D 键名非法");
    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_find_ref(void)
{
    CC_CASE("PARSE-013", "${find} 内嵌 ${ref}");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/find_ref.ccm", &ctx), "解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("读取 sources", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "sources", &v));
    CC_CHECK(v.type == CC_VAR_STRING, "结果为字符串");
    char text[4096];
    CCUINT32 n = v.as.string.len < sizeof(text) - 1 ? v.as.string.len : sizeof(text) - 1;
    memcpy(text, v.as.string.data, n);
    text[n] = '\0';
    CC_CHECK(strstr(text, "a.c") != NULL, "包含 a.c");
    CC_CHECK(strstr(text, "sub/c.c") != NULL, "包含 sub/c.c");
    CC_CHECK(strstr(text, "skip_test.c") == NULL, "排除测试文件");

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_comments(void)
{
    CC_CASE("PARSE-014", "第二阶段注释与字符串");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/comments/root.ccm", &ctx), "解析成功");
    CCVarValue v;
    CC_CHECK_EQ_I64("字符串中的 @include", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "text", &v));
    CC_CHECK(value_string_is(&v, "@include fake"), "字符串值");
    CC_CHECK_EQ_I64("注释外的 include 字段", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "a", &v));
    CC_CHECK_EQ_I64("a = 1", 1, v.as.integer);
    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_depth_limits(void)
{
    CC_CASE("PARSE-015", "值深度边界");
    CCMakeCCMContext ctx;

    CC_CHECK(parse_run_limits("tests/data/ccm/parse/depth_128.ccm", &ctx,
                              0, 0, 0, 128),
             "128 层嵌套在显式预算内成功");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run_limits("tests/data/ccm/parse/depth_129.ccm", &ctx,
                               0, 0, 0, 128),
             "129 层嵌套在显式预算下拒绝");
    parse_cleanup(&ctx);

    CC_CHECK(parse_run("tests/data/ccm/parse/depth_129.ccm", &ctx),
             "默认 0 预算不限制 129 层嵌套");
    parse_cleanup(&ctx);

    CC_CASE_END();
}

static void test_resource_limits(void)
{
    CC_CASE("PARSE-016", "资源上限与超长 token");
    CCMakeCCMContext ctx;

    CC_CHECK(!parse_run_limits("tests/data/ccm/parse/long_token.ccm",
                               &ctx, 8, 0, 0, 0),
             "超长字符串拒绝");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run_limits("tests/data/ccm/parse/limits_array.ccm",
                               &ctx, 0, 3, 0, 0),
             "数组元素超限拒绝");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run_limits("tests/data/ccm/parse/limits_object.ccm",
                               &ctx, 0, 0, 3, 0),
             "对象成员超限拒绝");
    parse_cleanup(&ctx);

    CC_CASE_END();
}

static void test_float_limits(void)
{
    CC_CASE("PARSE-017", "浮点有限性与范围");
    CCMakeCCMContext ctx;

    CC_CHECK(parse_run("tests/data/ccm/parse/float_max.ccm", &ctx),
             "最大有限浮点成功");
    CCVarValue v;
    CC_CHECK_EQ_I64("读取浮点", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "value", &v));
    CC_CHECK(v.type == CC_VAR_FLOAT, "浮点类型");
    CC_CHECK(v.as.floating > 1.7e308, "浮点值有效");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run("tests/data/ccm/parse/float_overflow.ccm", &ctx),
             "浮点溢出拒绝");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run("tests/data/ccm/parse/float_underflow.ccm", &ctx),
             "浮点下溢拒绝");
    parse_cleanup(&ctx);

    CC_CASE_END();
}

static void test_phase1_limits(void)
{
    CC_CASE("PARSE-020", "include 文件/边/深度预算");
    CCMakeCCMContext ctx;

    CC_CHECK(!parse_run_include_limits("tests/data/ccm/chain/root.ccm",
                                       &ctx, 2, 0, 0),
             "文件数量超限拒绝");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run_include_limits("tests/data/ccm/chain/root.ccm",
                                       &ctx, 0, 1, 0),
             "include 边数量超限拒绝");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run_include_limits("tests/data/ccm/chain/root.ccm",
                                       &ctx, 0, 0, 1),
             "include 深度超限拒绝");
    parse_cleanup(&ctx);

    CC_CASE_END();
}

static void test_find_result_limit(void)
{
    CC_CASE("PARSE-021", "${find} 结果预算");
    CCMakeCCMContext ctx;

    CC_CHECK(!parse_run_find_limit("tests/data/ccm/parse/find.ccm",
                                   &ctx, 1),
             "结果数量超限拒绝");
    parse_cleanup(&ctx);

    CC_CHECK(parse_run_find_limit("tests/data/ccm/parse/find_dup.ccm",
                                  &ctx, 1),
             "去重后的重复结果不触发上限");
    CCVarValue v;
    CC_CHECK_EQ_I64("读取 sources", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "sources", &v));
    CC_CHECK(v.type == CC_VAR_STRING, "结果为字符串");
    CC_CHECK(v.as.string.len > 0, "结果非空");
    parse_cleanup(&ctx);

    CC_CASE_END();
}

static void test_quoted_expansion(void)
{
    CC_CASE("PARSE-018", "内建函数引号边界");
    CCMakeCCMContext ctx;
    CC_CHECK(parse_run("tests/data/ccm/parse/quoted_ref.ccm", &ctx),
             "引号内 ${ref} 解析成功");

    CCVarValue v;
    CC_CHECK_EQ_I64("读取 sources", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "sources", &v));
    CC_CHECK(v.type == CC_VAR_STRING, "结果为字符串");
    char text[4096];
    CCUINT32 n = v.as.string.len < sizeof(text) - 1
                     ? v.as.string.len : sizeof(text) - 1;
    memcpy(text, v.as.string.data, n);
    text[n] = '\0';
    CC_CHECK(strstr(text, "a.c") != NULL, "包含 a.c");
    CC_CHECK(strstr(text, "skip_test.c") == NULL, "排除测试文件");

    parse_cleanup(&ctx);
    CC_CASE_END();
}

static void test_reuse_after_failure(void)
{
    CC_CASE("PARSE-019", "失败后重新解析");
    CCMakeCCMContext ctx;

    CC_CHECK(!parse_run("tests/data/ccm/parse/bad_key.ccm", &ctx),
             "第一次错误失败");
    parse_cleanup(&ctx);

    CC_CHECK(parse_run("tests/data/ccm/parse/basic.ccm", &ctx),
             "第一次正确成功");
    CCVarValue v;
    CC_CHECK_EQ_I64("项目字段", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "project", &v));
    CC_CHECK(value_string_is(&v, "demo"), "项目值");
    parse_cleanup(&ctx);

    CC_CHECK(!parse_run("tests/data/ccm/parse/bad_value.ccm", &ctx),
             "第二次错误失败");
    parse_cleanup(&ctx);

    CC_CHECK(parse_run("tests/data/ccm/parse/basic.ccm", &ctx),
             "第二次正确成功");
    CC_CHECK_EQ_I64("项目字段恢复", CC_VAR_OK,
                    ccvar_get(ccvar_global(), "project", &v));
    CC_CHECK(value_string_is(&v, "demo"), "项目值恢复");
    parse_cleanup(&ctx);

    CC_CASE_END();
}

static void test_deep_value_stack(void)
{
    CC_CASE("PARSE-022", "显式值帧栈深嵌套");
    const char *path = "out/ccm_deep_value.ccm";
    FILE *file = fopen(path, "wb");
    CC_CHECK(file != NULL, "创建深嵌套值文件");
    if (file)
    {
        fputs("\"deep\": ", file);
        for (CCUINT32 i = 0; i < 5000; i++)
            fputc('[', file);
        fputc('1', file);
        for (CCUINT32 i = 0; i < 5000; i++)
            fputc(']', file);
        fputs(";\n", file);
        fclose(file);

        CCMakeCCMContext ctx;
        CC_CHECK(parse_run(path, &ctx), "5000 层嵌套值解析");
        parse_cleanup(&ctx);
        remove(path);
    }
    CC_CASE_END();
}

static void test_ccm_dyn_safe(void)
{
    CC_CASE("PARSE-023", "CCM 动态追加边界");
    CCDyn dyn;
    memset(&dyn, 0, sizeof(dyn));
    dyn.size = (CCUINT32)-1 - 1;
    CCUINT32 value = 1;
    CC_CHECK(
        !ccm_dyn_push(&dyn, (CCUINT32)sizeof(value),
                      (CCINT32)sizeof(value), &value),
        "尺寸加法溢出被拒绝"
    );
    CC_CHECK(dyn.array == NULL, "失败不分配数组");
    CC_CHECK(
        !ccm_dyn_push(NULL, 1, 1, &value),
        "空动态数组拒绝"
    );
    CC_CHECK(
        !ccm_dyn_push(&dyn, 0, 1, &value),
        "零对齐拒绝"
    );
    CC_CHECK(
        !ccm_dyn_push(&dyn, 1, 0, &value),
        "零尺寸拒绝"
    );
    CC_CHECK(
        !ccm_dyn_push(&dyn, 1, 1, NULL),
        "空元素指针拒绝"
    );

    CCDyn ok_dyn;
    memset(&ok_dyn, 0, sizeof(ok_dyn));
    CC_CHECK(
        ccm_dyn_push(
            &ok_dyn, (CCUINT32)sizeof(value),
            (CCINT32)sizeof(value), &value
        ),
        "正常追加成功"
    );
    CC_CHECK(
        ok_dyn.size == (CCUINT32)sizeof(value),
        "正常追加大小"
    );
    if (ok_dyn.array)
        ccalloc(ok_dyn.array, 0, NULL);
    CC_CASE_END();
}

void test_ccm_parse(void)
{
    test_basic();
    test_order();
    test_merge();
    test_ref();
    test_arrays();
    test_array_objects();
    test_dedupe();
    test_empty();
    test_errors();
    test_find();
    test_find_ref();
    test_comments();
    test_depth_limits();
    test_resource_limits();
    test_float_limits();
    test_quoted_expansion();
    test_reuse_after_failure();
    test_phase1_limits();
    test_find_result_limit();
    test_defines();
    test_define_bad_key();
    test_deep_value_stack();
    test_ccm_dyn_safe();
}
