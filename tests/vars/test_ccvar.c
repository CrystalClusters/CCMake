/*
 * @Description: 全局变量树测试：路径写入、覆盖、对象合并、数组、删除、遍历。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "ccvar.h"

#include <string.h>

static CCVarResult set_str(CCVarTree *t, const char *p, const char *v, CCUINT32 len)
{
    return ccvar_set(t, p, CC_VAR_STRING, v, len, CC_VAR_WRITE_REPLACE);
}

static CCVarResult set_int(CCVarTree *t, const char *p, CCINT64 v)
{
    return ccvar_set(t, p, CC_VAR_INT, &v, 0, CC_VAR_WRITE_REPLACE);
}

static CCVarResult set_float(CCVarTree *t, const char *p, double v)
{
    return ccvar_set(t, p, CC_VAR_FLOAT, &v, 0, CC_VAR_WRITE_REPLACE);
}

static CCVarResult set_bool(CCVarTree *t, const char *p, CCBOOL v)
{
    return ccvar_set(t, p, CC_VAR_BOOL, &v, 0, CC_VAR_WRITE_REPLACE);
}

static CCVarResult set_obj(CCVarTree *t, const char *p, CCVarWriteMode mode)
{
    return ccvar_set(t, p, CC_VAR_OBJECT, NULL, 0, mode);
}

static CCVarResult set_arr(CCVarTree *t, const char *p)
{
    return ccvar_set(t, p, CC_VAR_ARRAY, NULL, 0, CC_VAR_WRITE_REPLACE);
}

static CCVarResult push_str(
    CCVarTree *t,
    const char *p,
    const char *v,
    CCUINT32 len,
    CCUINT32 *out
)
{
    return ccvar_array_push(t, p, CC_VAR_STRING, v, len, out);
}

static CCVarResult push_int(CCVarTree *t, const char *p, CCINT64 v, CCUINT32 *out)
{
    return ccvar_array_push(t, p, CC_VAR_INT, &v, 0, out);
}

static CCVarResult push_float(CCVarTree *t, const char *p, double v, CCUINT32 *out)
{
    return ccvar_array_push(t, p, CC_VAR_FLOAT, &v, 0, out);
}

static CCVarResult push_bool(CCVarTree *t, const char *p, CCBOOL v, CCUINT32 *out)
{
    return ccvar_array_push(t, p, CC_VAR_BOOL, &v, 0, out);
}

static CCVarResult push_obj(CCVarTree *t, const char *p, CCUINT32 *out)
{
    return ccvar_array_push(t, p, CC_VAR_OBJECT, NULL, 0, out);
}

static CCVarResult push_arr(CCVarTree *t, const char *p, CCUINT32 *out)
{
    return ccvar_array_push(t, p, CC_VAR_ARRAY, NULL, 0, out);
}

static CCBOOL value_string_is(const CCVarValue *v, const char *expect)
{
    if (v->type != CC_VAR_STRING)
        return CCFALSE;
    size_t len = strlen(expect);
    return (
        v->as.string.len == len &&
        memcmp(v->as.string.data, expect, len) == 0
    ) ? CCTRUE : CCFALSE;
}

static void test_scalars(void)
{
    CC_CASE("VARS-001", "标量写入与覆盖");
    CCVarTree *tree = ccvar_create();
    CC_CHECK(tree != NULL, "创建树");

    CC_CHECK_EQ_I64("写字符串", CC_VAR_OK, set_str(tree, "cc", "gcc", 3));
    CC_CHECK_EQ_I64("写整数", CC_VAR_OK, set_int(tree, "port", 8080));
    CC_CHECK_EQ_I64("写浮点", CC_VAR_OK, set_float(tree, "ratio", 1.5));
    CC_CHECK_EQ_I64("写布尔", CC_VAR_OK, set_bool(tree, "debug", CCTRUE));

    CCVarValue v;
    CC_CHECK_EQ_I64("读字符串", CC_VAR_OK, ccvar_get(tree, "cc", &v));
    CC_CHECK(value_string_is(&v, "gcc"), "字符串值");
    CC_CHECK_EQ_I64("读整数", CC_VAR_OK, ccvar_get(tree, "port", &v));
    CC_CHECK_EQ_I64("整数值", 8080, v.as.integer);
    CC_CHECK_EQ_I64("读浮点", CC_VAR_OK, ccvar_get(tree, "ratio", &v));
    CC_CHECK(v.as.floating > 1.49 && v.as.floating < 1.51, "浮点值");
    CC_CHECK_EQ_I64("读布尔", CC_VAR_OK, ccvar_get(tree, "debug", &v));
    CC_CHECK(v.as.boolean == CCTRUE, "布尔值");

    CC_CHECK_EQ_I64("覆盖类型", CC_VAR_OK, set_str(tree, "port", "x", 1));
    CC_CHECK_EQ_I64("覆盖后类型", CC_VAR_STRING, ccvar_type(tree, "port"));

    /* 对象子树被标量覆盖，随后又被数组覆盖 */
    CC_CHECK_EQ_I64("对象", CC_VAR_OK, set_obj(tree, "mix", CC_VAR_WRITE_REPLACE));
    CC_CHECK_EQ_I64("对象成员", CC_VAR_OK, set_str(tree, "mix.x", "1", 1));
    CC_CHECK_EQ_I64("对象覆盖为标量", CC_VAR_OK, set_int(tree, "mix", 7));
    CC_CHECK_EQ_I64("标量类型", CC_VAR_INT, ccvar_type(tree, "mix"));
    CC_CHECK_EQ_I64("旧对象子树已删除", 0, ccvar_child_count(tree, "mix"));
    CC_CHECK_EQ_I64("标量覆盖为数组", CC_VAR_OK, set_arr(tree, "mix"));
    CC_CHECK_EQ_I64("数组类型", CC_VAR_ARRAY, ccvar_type(tree, "mix"));
    CC_CHECK_EQ_I64("空数组", 0, ccvar_array_size(tree, "mix"));

    ccvar_destroy(tree);
    CC_CASE_END();
}

static void test_nested(void)
{
    CC_CASE("VARS-002", "嵌套对象与浅合并");
    CCVarTree *tree = ccvar_create();

    CC_CHECK_EQ_I64("创建对象", CC_VAR_OK, set_obj(tree, "toolchain", CC_VAR_WRITE_MERGE));
    CC_CHECK_EQ_I64("写成员", CC_VAR_OK, set_str(tree, "toolchain.c", "gcc", 3));
    CC_CHECK_EQ_I64("写成员", CC_VAR_OK, set_str(tree, "toolchain.linker", "ld", 2));

    CC_CHECK_EQ_I64("再次合并", CC_VAR_OK, set_obj(tree, "toolchain", CC_VAR_WRITE_MERGE));
    CC_CHECK_EQ_I64("覆写成员", CC_VAR_OK, set_str(tree, "toolchain.c", "clang", 5));

    CCVarValue v;
    CC_CHECK_EQ_I64("读覆写成员", CC_VAR_OK, ccvar_get(tree, "toolchain.c", &v));
    CC_CHECK(value_string_is(&v, "clang"), "覆写值");
    CC_CHECK_EQ_I64("未提及成员保留", CC_VAR_OK, ccvar_get(tree, "toolchain.linker", &v));
    CC_CHECK(value_string_is(&v, "ld"), "保留值");
    CC_CHECK_EQ_I64("对象子节点数", 2, ccvar_child_count(tree, "toolchain"));

    /* 嵌套对象成员使用 REPLACE：旧子树整体替换 */
    CC_CHECK_EQ_I64("嵌套对象", CC_VAR_OK, set_obj(tree, "toolchain.flags", CC_VAR_WRITE_REPLACE));
    CC_CHECK_EQ_I64("嵌套成员", CC_VAR_OK, set_int(tree, "toolchain.flags.a", 1));
    CC_CHECK_EQ_I64("嵌套对象替换", CC_VAR_OK, set_obj(tree, "toolchain.flags", CC_VAR_WRITE_REPLACE));
    CC_CHECK_EQ_I64("旧成员已删除", CC_VAR_NONE, ccvar_type(tree, "toolchain.flags.a"));

    ccvar_destroy(tree);
    CC_CASE_END();
}

static void test_arrays(void)
{
    CC_CASE("VARS-003", "数组写入、索引与元素删除");
    CCVarTree *tree = ccvar_create();

    CC_CHECK_EQ_I64("创建数组", CC_VAR_OK, set_arr(tree, "defines"));
    CCUINT32 idx = 0;
    CC_CHECK_EQ_I64("追加字符串", CC_VAR_OK, push_str(tree, "defines", "A", 1, &idx));
    CC_CHECK_EQ_I64("首个下标", 0, idx);
    CC_CHECK_EQ_I64("追加字符串", CC_VAR_OK, push_str(tree, "defines", "B", 1, &idx));
    CC_CHECK_EQ_I64("第二个下标", 1, idx);
    CC_CHECK_EQ_I64("数组长度", 2, ccvar_array_size(tree, "defines"));

    CCVarValue v;
    CC_CHECK_EQ_I64("读 [0]", CC_VAR_OK, ccvar_get(tree, "defines[0]", &v));
    CC_CHECK(value_string_is(&v, "A"), "[0] 值");
    CC_CHECK_EQ_I64("读 [1]", CC_VAR_OK, ccvar_get(tree, "defines[1]", &v));
    CC_CHECK(value_string_is(&v, "B"), "[1] 值");
    CC_CHECK_EQ_I64("越界", CC_VAR_ERR_NOENT, ccvar_get(tree, "defines[2]", &v));

    CC_CHECK_EQ_I64("删除 [0]", CC_VAR_OK, ccvar_remove(tree, "defines[0]"));
    CC_CHECK_EQ_I64("删除后长度", 1, ccvar_array_size(tree, "defines"));
    CC_CHECK_EQ_I64("原 [1] 前移", CC_VAR_OK, ccvar_get(tree, "defines[0]", &v));
    CC_CHECK(value_string_is(&v, "B"), "前移值");

    CC_CHECK_EQ_I64("嵌套数组对象", CC_VAR_OK, set_arr(tree, "targets"));
    CCUINT32 obj = 0;
    CC_CHECK_EQ_I64("追加对象", CC_VAR_OK, push_obj(tree, "targets", &obj));
    CC_CHECK_EQ_I64("对象下标", 0, obj);
    CC_CHECK_EQ_I64("写对象成员", CC_VAR_OK, set_str(tree, "targets[0].name", "app", 3));
    CC_CHECK_EQ_I64("读对象成员", CC_VAR_OK, ccvar_get(tree, "targets[0].name", &v));
    CC_CHECK(value_string_is(&v, "app"), "数组对象成员");

    CC_CHECK_EQ_I64("删除对象元素", CC_VAR_OK, ccvar_remove(tree, "targets[0]"));
    CC_CHECK_EQ_I64("对象数组长度", 0, ccvar_array_size(tree, "targets"));

    /* 数组元素类型覆盖：浮点、布尔、嵌套数组 */
    CC_CHECK_EQ_I64("额外数组", CC_VAR_OK, set_arr(tree, "extra"));
    CC_CHECK_EQ_I64("推入浮点", CC_VAR_OK, push_float(tree, "extra", 2.5, NULL));
    CC_CHECK_EQ_I64("推入布尔", CC_VAR_OK, push_bool(tree, "extra", CCTRUE, NULL));
    CC_CHECK_EQ_I64("推入数组", CC_VAR_OK, push_arr(tree, "extra", NULL));
    CC_CHECK_EQ_I64("额外数组长度", 3, ccvar_array_size(tree, "extra"));
    CC_CHECK_EQ_I64("浮点元素类型", CC_VAR_FLOAT, ccvar_type(tree, "extra[0]"));
    CC_CHECK_EQ_I64("布尔元素类型", CC_VAR_BOOL, ccvar_type(tree, "extra[1]"));
    CC_CHECK_EQ_I64("数组元素类型", CC_VAR_ARRAY, ccvar_type(tree, "extra[2]"));

    ccvar_destroy(tree);
    CC_CASE_END();
}

static void test_delete_subtree(void)
{
    CC_CASE("VARS-004", "对象子树删除与数组元素重排");
    CCVarTree *tree = ccvar_create();

    CC_CHECK_EQ_I64("对象", CC_VAR_OK, set_obj(tree, "a", CC_VAR_WRITE_REPLACE));
    CC_CHECK_EQ_I64("子对象", CC_VAR_OK, set_obj(tree, "a.b", CC_VAR_WRITE_REPLACE));
    CC_CHECK_EQ_I64("子值", CC_VAR_OK, set_int(tree, "a.b.c", 1));
    CC_CHECK_EQ_I64("兄弟值", CC_VAR_OK, set_int(tree, "a.d", 2));
    CC_CHECK_EQ_I64("数组", CC_VAR_OK, set_arr(tree, "a.list"));
    for (CCUINT32 i = 0; i < 5; i++)
    {
        CC_CHECK_EQ_I64(
            "追加", CC_VAR_OK,
            push_int(tree, "a.list", (CCINT64)i, NULL)
        );
    }

    CC_CHECK_EQ_I64("删除子树", CC_VAR_OK, ccvar_remove(tree, "a.b"));
    CC_CHECK_EQ_I64("子树已无", CC_VAR_NONE, ccvar_type(tree, "a.b.c"));
    CC_CHECK_EQ_I64("兄弟保留", CC_VAR_INT, ccvar_type(tree, "a.d"));
    CC_CHECK_EQ_I64("数组保留", 5, ccvar_array_size(tree, "a.list"));
    CCVarValue v;
    CC_CHECK_EQ_I64("数组 [2]", CC_VAR_OK, ccvar_get(tree, "a.list[2]", &v));
    CC_CHECK_EQ_I64("数组值", 2, v.as.integer);

    /* 删除整个对象，数组中的引用也要一起回收 */
    CC_CHECK_EQ_I64("删除对象", CC_VAR_OK, ccvar_remove(tree, "a"));
    CC_CHECK_EQ_I64("对象已无", CC_VAR_NONE, ccvar_type(tree, "a"));
    CC_CHECK_EQ_I64("根子节点数", 0, ccvar_child_count(tree, ""));

    ccvar_destroy(tree);
    CC_CASE_END();
}

static void test_iteration_and_keys(void)
{
    CC_CASE("VARS-005", "子节点遍历与键名规则");
    CCVarTree *tree = ccvar_create();

    CC_CHECK_EQ_I64("键 a", CC_VAR_OK, set_int(tree, "b", 1));
    CC_CHECK_EQ_I64("键 b", CC_VAR_OK, set_int(tree, "a", 2));
    CC_CHECK_EQ_I64("键 c", CC_VAR_OK, set_int(tree, "c", 3));
    CC_CHECK_EQ_I64("子节点数", 3, ccvar_child_count(tree, ""));

    CC_CHECK(ccvar_key_valid("source_dir", 10), "合法键");
    CC_CHECK(ccvar_key_valid("_x1", 3), "合法键");
    CC_CHECK(!ccvar_key_valid("a.b", 3), "点号非法");
    CC_CHECK(!ccvar_key_valid("a[0]", 4), "下标非法");
    CC_CHECK(!ccvar_key_valid("foo-bar", 7), "连字符非法");
    CC_CHECK(!ccvar_key_valid("", 0), "空键非法");
    CC_CHECK(!ccvar_key_valid("1a", 2), "数字开头非法");

    CC_CHECK_EQ_I64("非法键写入", CC_VAR_ERR_PARAM, set_int(tree, "bad-key", 1));

    ccvar_destroy(tree);
    CC_CASE_END();
}

static void test_many_keys(void)
{
    CC_CASE("VARS-006", "大量同层键与节点搬移");
    CCVarTree *tree = ccvar_create();
    char path[32];

    for (CCUINT32 i = 0; i < 200; i++)
    {
        snprintf(path, sizeof(path), "k%u", i);
        CC_CHECK_EQ_I64("写入", CC_VAR_OK, set_int(tree, path, (CCINT64)i));
    }
    CC_CHECK_EQ_I64("键数量", 200, ccvar_child_count(tree, ""));

    for (CCUINT32 i = 0; i < 200; i += 2)
    {
        snprintf(path, sizeof(path), "k%u", i);
        CC_CHECK_EQ_I64("删除", CC_VAR_OK, ccvar_remove(tree, path));
    }
    CC_CHECK_EQ_I64("删除后数量", 100, ccvar_child_count(tree, ""));

    CCVarValue v;
    CC_CHECK_EQ_I64("奇数保留", CC_VAR_OK, ccvar_get(tree, "k199", &v));
    CC_CHECK_EQ_I64("奇数值", 199, v.as.integer);

    ccvar_destroy(tree);
    CC_CASE_END();
}

static void test_clear(void)
{
    CC_CASE("VARS-007", "清空与复用");
    CCVarTree *tree = ccvar_create();
    CC_CHECK_EQ_I64("写值", CC_VAR_OK, set_int(tree, "a", 1));
    CC_CHECK_EQ_I64("清空后无值", CC_VAR_NONE, (ccvar_clear(tree), ccvar_type(tree, "a")));
    CC_CHECK_EQ_I64("清空后可写", CC_VAR_OK, set_int(tree, "b", 2));
    CCVarValue v;
    CC_CHECK_EQ_I64("读新值", CC_VAR_OK, ccvar_get(tree, "b", &v));
    CC_CHECK_EQ_I64("新值正确", 2, v.as.integer);
    ccvar_destroy(tree);
    CC_CASE_END();
}

void test_ccvar(void)
{
    test_scalars();
    test_nested();
    test_arrays();
    test_delete_subtree();
    test_iteration_and_keys();
    test_many_keys();
    test_clear();
}
