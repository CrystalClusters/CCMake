/*
 * @Description: CMK 值模型测试：类型、字符串、列表、目标引用与动作。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_value.h"
#include "cmk/cmk_safe.h"

#include <string.h>

static CCBOOL type_is(const CMKType *type, CMKValueType kind)
{
    return (type && type->kind == kind) ? CCTRUE : CCFALSE;
}

void test_cmk_value(void)
{
    /* ------------------------------------------------------------------
     * VAL-001 类型与格式化
     * ---------------------------------------------------------------- */
    CC_CASE("VAL-001", "类型与格式化");
    {
        CMKType integer = cmk_type_scalar(CMK_VALUE_INT);
        CMKType string_list = cmk_type_list(CMK_VALUE_STRING);
        CC_CHECK(type_is(&integer, CMK_VALUE_INT), "标量类型");
        CC_CHECK(
            type_is(&string_list, CMK_VALUE_LIST) &&
            string_list.element == CMK_VALUE_STRING,
            "列表类型"
        );
        CC_CHECK(
            cmk_type_equal(&integer, &integer),
            "类型自相等"
        );
        CC_CHECK(
            !cmk_type_equal(&integer, &string_list),
            "类型不相等"
        );
        CC_CHECK(cmk_type_is_numeric(&integer), "int 是数值");
        CC_CHECK(!cmk_type_is_scalar(&string_list), "list 不是标量");

        char buf[64];
        CC_CHECK(cmk_type_format(&string_list, buf, sizeof(buf)), "格式化 list");
        CC_CHECK_EQ_STR("list 类型文本", "list<string>", buf);
        CC_CHECK(cmk_type_format(&integer, buf, sizeof(buf)), "格式化 int");
        CC_CHECK_EQ_STR("int 类型文本", "int", buf);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * VAL-002 字符串所有权
     * ---------------------------------------------------------------- */
    CC_CASE("VAL-002", "字符串所有权");
    {
        char source[] = "hello";
        CMKString text;
        cmk_string_init(&text);
        CC_CHECK(cmk_string_set(&text, source, 5), "设置字符串");
        source[0] = 'X';
        CC_CHECK_EQ_STR("字符串复制", "hello", text.data);
        CC_CHECK_EQ_I64("字符串长度", 5, (CCINT64)text.len);

        CC_CHECK(cmk_string_append(&text, " world", 6), "追加字符串");
        CC_CHECK_EQ_STR("追加结果", "hello world", text.data);
        CC_CHECK_EQ_I64("追加长度", 11, (CCINT64)text.len);

        CMKString empty;
        cmk_string_init(&empty);
        CC_CHECK(cmk_string_set(&empty, NULL, 0), "空字符串");
        CC_CHECK(empty.data != NULL, "空字符串有缓冲区");
        CC_CHECK(cmk_string_equal(&empty, &empty), "空字符串相等");
        cmk_string_clear(&empty);
        cmk_string_clear(&text);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * VAL-003 标量与列表
     * ---------------------------------------------------------------- */
    CC_CASE("VAL-003", "标量与列表");
    {
        CMKValue integer;
        cmk_value_init(&integer);
        CC_CHECK(cmk_value_set_int(&integer, 42), "设置 int");
        CCINT64 number = 0;
        CC_CHECK(cmk_value_as_int(&integer, &number), "读取 int");
        CC_CHECK_EQ_I64("int 值", 42, number);

        CMKValue list;
        cmk_value_init(&list);
        CC_CHECK(cmk_value_set_list(&list, CMK_VALUE_INT), "创建列表");
        CC_CHECK(cmk_list_append(list.as.list, &integer), "追加元素");
        CC_CHECK(
            cmk_value_set_int(&integer, 7),
            "覆盖 int 不影响列表"
        );
        CC_CHECK_EQ_I64("列表元素数", 1, (CCINT64)cmk_list_count(list.as.list));
        CC_CHECK_EQ_I64(
            "列表元素值", 42,
            cmk_list_at(list.as.list, 0)->as.integer
        );

        CMKValue copy;
        cmk_value_init(&copy);
        CC_CHECK(cmk_value_copy(&copy, &list), "复制列表");
        CC_CHECK(cmk_value_equal(&list, &copy), "列表相等");
        cmk_value_clear(&list);
        CC_CHECK_EQ_I64(
            "复制后列表仍有效", 42,
            cmk_list_at(copy.as.list, 0)->as.integer
        );
        cmk_value_clear(&copy);
        cmk_value_clear(&integer);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * VAL-004 目标引用与动作
     * ---------------------------------------------------------------- */
    CC_CASE("VAL-004", "动作");
    {
        CMKValue action;
        cmk_value_init(&action);
        CC_CHECK(cmk_value_set_action(&action, CMK_ACTION_EXEC), "创建动作");
        CC_CHECK(
            cmk_string_set(&action.as.action->program, "cc", 2),
            "动作程序"
        );
        action.as.action->args = cmk_list_create(CMK_VALUE_STRING);
        CC_CHECK(action.as.action->args != NULL, "动作参数列表");
        CMKValue flag;
        cmk_value_init(&flag);
        CC_CHECK(cmk_value_set_string(&flag, "-O2", 3), "参数字符串");
        CC_CHECK(cmk_list_append(action.as.action->args, &flag), "追加参数");
        cmk_value_clear(&flag);

        CMKValue action_copy;
        cmk_value_init(&action_copy);
        CC_CHECK(cmk_value_copy(&action_copy, &action), "复制动作");
        cmk_value_clear(&action);
        CC_CHECK(
            action_copy.type == CMK_VALUE_ACTION &&
            action_copy.as.action->kind == CMK_ACTION_EXEC &&
            action_copy.as.action->args != NULL &&
            cmk_list_count(action_copy.as.action->args) == 1,
            "动作深复制"
        );
        cmk_value_clear(&action_copy);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * VAL-005 文本化
     * ---------------------------------------------------------------- */
    CC_CASE("VAL-005", "文本化");
    {
        CMKValue value;
        cmk_value_init(&value);
        CMKString text;
        cmk_string_init(&text);

        CC_CHECK(cmk_value_set_int(&value, -3), "int 文本化输入");
        CC_CHECK(cmk_value_to_string(&value, &text), "int 文本化");
        CC_CHECK_EQ_STR("int 文本", "-3", text.data);

        CC_CHECK(cmk_value_set_bool(&value, CCTRUE), "bool 文本化输入");
        CC_CHECK(cmk_value_to_string(&value, &text), "bool 文本化");
        CC_CHECK_EQ_STR("bool 文本", "true", text.data);

        CC_CHECK(cmk_value_set_list(&value, CMK_VALUE_INT), "列表文本化输入");
        CMKValue item;
        cmk_value_init(&item);
        CC_CHECK(cmk_value_set_int(&item, 1), "列表元素");
        CC_CHECK(cmk_list_append(value.as.list, &item), "追加列表元素");
        CC_CHECK(cmk_value_set_int(&item, 2), "第二元素");
        CC_CHECK(cmk_list_append(value.as.list, &item), "追加第二元素");
        CC_CHECK(cmk_value_to_string(&value, &text), "列表文本化");
        CC_CHECK_EQ_STR("列表文本", "[1, 2]", text.data);

        cmk_value_clear(&item);
        cmk_value_clear(&value);
        cmk_string_clear(&text);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * VAL-006 尺寸运算与动态追加边界
     * ---------------------------------------------------------------- */
    CC_CASE("VAL-006", "尺寸运算与动态追加边界");
    {
        CCUINT64 out = 0;
        CC_CHECK(
            !cmk_safe_add_u64((CCUINT64)-1, 1, &out),
            "u64 加法溢出拒绝"
        );
        CC_CHECK(
            cmk_safe_add_u64(2, 3, &out) && out == 5,
            "u64 加法正常"
        );
        CC_CHECK(
            !cmk_safe_mul_u64((CCUINT64)-1, 2, &out),
            "u64 乘法溢出拒绝"
        );
        CC_CHECK(
            cmk_safe_mul_u64(6, 7, &out) && out == 42,
            "u64 乘法正常"
        );

        CCDyn dyn;
        memset(&dyn, 0, sizeof(dyn));
        dyn.size = (CCUINT32)-1 - 1;
        CCUINT32 value = 1;
        CC_CHECK(
            !cmk_dyn_push(
                &dyn, (CCUINT32)sizeof(value),
                (CCINT32)sizeof(value), &value
            ),
            "动态追加尺寸溢出拒绝"
        );
        CC_CHECK(dyn.array == NULL, "溢出失败不分配数组");
        CC_CHECK(
            !cmk_dyn_push(NULL, 1, 1, &value),
            "空动态数组拒绝"
        );
        CC_CHECK(
            !cmk_dyn_push(&dyn, 0, 1, &value),
            "零对齐拒绝"
        );
        CC_CHECK(
            !cmk_dyn_push(&dyn, 1, 0, &value),
            "零尺寸拒绝"
        );
        CC_CHECK(
            !cmk_dyn_push(&dyn, 1, 1, NULL),
            "空元素指针拒绝"
        );
    }
    CC_CASE_END();
}
