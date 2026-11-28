/*
 * @Description: MK 内建注册表与标准内建测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "mk/mk_builtin.h"

#include <string.h>

static CCBOOL make_string(MKValue *value, const char *text)
{
    mk_value_init(value);
    return mk_value_set_string(value, text, (CCUINT32)strlen(text));
}

static CCBOOL make_string_list(
    MKValue *value, const char *const *items, CCUINT32 count
)
{
    mk_value_init(value);
    if (!mk_value_set_list(value, MK_VALUE_STRING))
        return CCFALSE;
    for (CCUINT32 i = 0; i < count; i++)
    {
        MKValue item;
        if (!make_string(&item, items[i]))
            return CCFALSE;
        CCBOOL ok = mk_list_append(value->as.list, &item);
        mk_value_clear(&item);
        if (!ok)
            return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL call_builtin(
    const MKBuiltinRegistry *registry, const char *name,
    MKValue *args, CCUINT32 argc, MKValue *out,
    MKDiagnostic *diagnostic
)
{
    const MKBuiltinSpec *spec = mk_builtin_find(
        registry, name, (CCUINT32)strlen(name)
    );
    if (!spec)
        return CCFALSE;

    mk_diagnostic_clear(diagnostic);
    mk_value_init(out);
    MKBuiltinCall call;
    memset(&call, 0, sizeof(call));
    call.runtime = NULL;
    call.args = args;
    call.argc = argc;
    call.out = out;
    call.diagnostic = diagnostic;
    call.file = "builtin.mk";
    call.line = 1;
    call.col = 1;
    return spec->fn(&call);
}

void test_mk_builtin(void)
{
    /* ------------------------------------------------------------------
     * BLT-001 注册表
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-001", "注册表");
    {
        MKBuiltinRegistry *registry = mk_builtin_registry_create();
        CC_CHECK(registry != NULL, "创建注册表");
        CC_CHECK(mk_builtin_register_standard(registry), "注册标准内建");
        CC_CHECK(
            mk_builtin_find(registry, "len", 3) != NULL,
            "查找 len"
        );
        CC_CHECK(
            mk_builtin_find(registry, "command", 7) != NULL,
            "查找 command"
        );
        CC_CHECK(
            mk_builtin_find(registry, "missing", 7) == NULL,
            "未知内建"
        );
        CC_CHECK(
            !mk_builtin_register_standard(registry),
            "重复注册失败"
        );

        MKBuiltinSpec invalid;
        memset(&invalid, 0, sizeof(invalid));
        invalid.abi_version = MK_BUILTIN_ABI_VERSION + 1;
        invalid.name = "bad";
        CC_CHECK(!mk_builtin_register(registry, &invalid), "ABI 不匹配");

        mk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-002 纯计算与查询
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-002", "纯计算与查询");
    {
        MKBuiltinRegistry *registry = mk_builtin_registry_create();
        CC_CHECK(mk_builtin_register_standard(registry), "注册标准内建");
        MKDiagnostic diagnostic;
        mk_diagnostic_init(&diagnostic);

        const char *items[] = {"a", "b", "c"};
        MKValue list;
        CC_CHECK(make_string_list(&list, items, 3), "构造字符串列表");
        MKValue args[3];
        MKValue out;
        args[0] = list;
        CC_CHECK(
            call_builtin(registry, "len", args, 1, &out, &diagnostic),
            "len 调用"
        );
        CC_CHECK_EQ_I64("len 结果", 3, out.as.integer);
        mk_value_clear(&out);

        MKValue sep;
        CC_CHECK(make_string(&sep, ", "), "分隔符");
        args[0] = list;
        args[1] = sep;
        CC_CHECK(
            call_builtin(registry, "join", args, 2, &out, &diagnostic),
            "join 调用"
        );
        CC_CHECK_EQ_STR("join 结果", "a, b, c", out.as.string.data);
        mk_value_clear(&out);

        MKValue text;
        MKValue from;
        MKValue to;
        CC_CHECK(make_string(&text, "src/main.c"), "subst 输入");
        CC_CHECK(make_string(&from, ".c"), "subst from");
        CC_CHECK(make_string(&to, ".o"), "subst to");
        args[0] = from;
        args[1] = to;
        args[2] = text;
        CC_CHECK(
            call_builtin(registry, "subst", args, 3, &out, &diagnostic),
            "subst 调用"
        );
        CC_CHECK_EQ_STR("subst 结果", "src/main.o", out.as.string.data);
        mk_value_clear(&out);

        MKValue pattern;
        MKValue replacement;
        MKValue pat_text;
        CC_CHECK(make_string(&pattern, "%.c"), "patsubst pattern");
        CC_CHECK(make_string(&replacement, "%.o"), "patsubst replacement");
        CC_CHECK(
            make_string(&pat_text, "a.c b.c c.h"),
            "patsubst text"
        );
        args[0] = pattern;
        args[1] = replacement;
        args[2] = pat_text;
        CC_CHECK(
            call_builtin(
                registry, "patsubst", args, 3, &out, &diagnostic
            ),
            "patsubst 调用"
        );
        CC_CHECK_EQ_STR(
            "patsubst 结果", "a.o b.o c.h", out.as.string.data
        );
        mk_value_clear(&out);
        mk_value_clear(&pat_text);
        CC_CHECK(
            make_string(&pat_text, "  a.c\tb.c  "),
            "patsubst 空白文本"
        );
        args[0] = pattern;
        args[1] = replacement;
        args[2] = pat_text;
        CC_CHECK(
            call_builtin(
                registry, "patsubst", args, 3, &out, &diagnostic
            ),
            "patsubst 空白调用"
        );
        CC_CHECK_EQ_STR(
            "patsubst 空白结果", "a.o b.o", out.as.string.data
        );
        mk_value_clear(&out);
        mk_value_clear(&pat_text);
        CC_CHECK(make_string(&pat_text, ""), "patsubst 空文本");
        args[0] = pattern;
        args[1] = replacement;
        args[2] = pat_text;
        CC_CHECK(
            call_builtin(
                registry, "patsubst", args, 3, &out, &diagnostic
            ),
            "patsubst 空文本调用"
        );
        CC_CHECK_EQ_STR("patsubst 空文本结果", "", out.as.string.data);
        mk_value_clear(&out);

        mk_value_clear(&pattern);
        mk_value_clear(&replacement);
        mk_value_clear(&pat_text);

        CC_CHECK(make_string(&pattern, "a.c"), "patsubst 无通配模式");
        CC_CHECK(make_string(&replacement, "x"), "patsubst 字面替换");
        CC_CHECK(make_string(&pat_text, "a.c b.c"), "patsubst 无通配文本");
        args[0] = pattern;
        args[1] = replacement;
        args[2] = pat_text;
        CC_CHECK(
            call_builtin(
                registry, "patsubst", args, 3, &out, &diagnostic
            ),
            "patsubst 无通配调用"
        );
        CC_CHECK_EQ_STR("patsubst 无通配结果", "x b.c", out.as.string.data);
        mk_value_clear(&out);
        mk_value_clear(&pattern);
        mk_value_clear(&replacement);
        mk_value_clear(&pat_text);

        CC_CHECK(make_string(&pattern, "%.c"), "patsubst 空 stem 模式");
        CC_CHECK(make_string(&replacement, "%.o"), "patsubst 空 stem 替换");
        CC_CHECK(make_string(&pat_text, ".c"), "patsubst 空 stem 文本");
        args[0] = pattern;
        args[1] = replacement;
        args[2] = pat_text;
        CC_CHECK(
            call_builtin(
                registry, "patsubst", args, 3, &out, &diagnostic
            ),
            "patsubst 空 stem 调用"
        );
        CC_CHECK_EQ_STR("patsubst 空 stem 结果", ".o", out.as.string.data);
        mk_value_clear(&out);
        mk_value_clear(&pattern);
        mk_value_clear(&replacement);
        mk_value_clear(&pat_text);

        mk_value_clear(&from);
        mk_value_clear(&to);
        mk_value_clear(&text);
        CC_CHECK(make_string(&from, ""), "subst 空 from");
        CC_CHECK(make_string(&to, "x"), "subst 空 from 替换");
        CC_CHECK(make_string(&text, "abc"), "subst 空 from 文本");
        args[0] = from;
        args[1] = to;
        args[2] = text;
        CC_CHECK(
            call_builtin(registry, "subst", args, 3, &out, &diagnostic),
            "subst 空 from 调用"
        );
        CC_CHECK_EQ_STR("subst 空 from 结果", "abc", out.as.string.data);
        mk_value_clear(&out);
        mk_value_clear(&from);
        mk_value_clear(&to);
        mk_value_clear(&text);
        CC_CHECK(make_string(&text, "src/main.c"), "恢复 basename 输入");
        mk_value_clear(&pattern);
        mk_value_clear(&replacement);
        mk_value_clear(&pat_text);

        args[0] = text;
        CC_CHECK(
            call_builtin(registry, "basename", args, 1, &out, &diagnostic),
            "basename 调用"
        );
        CC_CHECK_EQ_STR("basename 结果", "main.c", out.as.string.data);
        mk_value_clear(&out);

        CC_CHECK(
            call_builtin(registry, "dirname", args, 1, &out, &diagnostic),
            "dirname 调用"
        );
        CC_CHECK_EQ_STR("dirname 结果", "src", out.as.string.data);
        mk_value_clear(&out);

        MKValue key;
        CC_CHECK(make_string(&key, "os"), "host_info 键");
        args[0] = key;
        CC_CHECK(
            call_builtin(registry, "host_info", args, 1, &out, &diagnostic),
            "host_info 调用"
        );
        CC_CHECK(out.type == MK_VALUE_STRING, "host_info 返回字符串");
        mk_value_clear(&out);

        mk_value_clear(&key);
        CC_CHECK(make_string(&key, "nope"), "非法 host_info 键");
        args[0] = key;
        CC_CHECK(
            !call_builtin(registry, "host_info", args, 1, &out, &diagnostic),
            "非法 host_info 键失败"
        );
        mk_value_clear(&out);

        mk_value_clear(&list);
        mk_value_clear(&sep);
        mk_value_clear(&text);
        mk_value_clear(&from);
        mk_value_clear(&to);
        mk_value_clear(&key);
        mk_diagnostic_clear(&diagnostic);
        mk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-004 参数错误
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-004", "参数错误");
    {
        MKBuiltinRegistry *registry = mk_builtin_registry_create();
        CC_CHECK(mk_builtin_register_standard(registry), "注册标准内建");
        MKDiagnostic diagnostic;
        mk_diagnostic_init(&diagnostic);

        MKValue args[2];
        MKValue out;
        mk_value_init(&out);
        CC_CHECK(make_string(&args[0], "only"), "单参数");
        CC_CHECK(
            !call_builtin(registry, "command", args, 1, &out, &diagnostic),
            "command 参数不足"
        );
        mk_value_clear(&out);

        mk_value_clear(&args[0]);
        mk_diagnostic_clear(&diagnostic);
        mk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-005 find_files
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-005", "find_files");
    {
        MKBuiltinRegistry *registry = mk_builtin_registry_create();
        CC_CHECK(mk_builtin_register_standard(registry), "注册标准内建");
        MKDiagnostic diagnostic;
        mk_diagnostic_init(&diagnostic);

        MKValue pattern;
        CC_CHECK(make_string(&pattern, "*.c"), "find pattern");
        const char *roots[] = {"tests/data/mk/find"};
        MKValue root_list;
        CC_CHECK(make_string_list(&root_list, roots, 1), "find roots");
        MKValue args[2] = {pattern, root_list};
        MKValue out;
        CC_CHECK(
            call_builtin(
                registry, "find_files", args, 2, &out, &diagnostic
            ),
            "find_files 调用"
        );
        CC_CHECK_EQ_I64(
            "find_files 结果数", 3,
            (CCINT64)mk_list_count(out.as.list)
        );
        const MKValue *first = mk_list_at(out.as.list, 0);
        const MKValue *second = mk_list_at(out.as.list, 1);
        const MKValue *third = mk_list_at(out.as.list, 2);
        CC_CHECK(
            first && second && third &&
            strstr(first->as.string.data, "find/a.c") != NULL &&
            strstr(second->as.string.data, "find/sub/b.c") != NULL &&
            strstr(third->as.string.data, "find/sub/deep/c.c") != NULL,
            "find_files 结果排序"
        );
        mk_value_clear(&out);
        mk_value_clear(&pattern);
        mk_value_clear(&root_list);
        mk_diagnostic_clear(&diagnostic);
        mk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-006 文件状态
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-006", "文件状态");
    {
        MKBuiltinRegistry *registry = mk_builtin_registry_create();
        CC_CHECK(mk_builtin_register_standard(registry), "注册标准内建");
        MKDiagnostic diagnostic;
        mk_diagnostic_init(&diagnostic);

        MKValue path;
        CC_CHECK(
            make_string(&path, "tests/data/mk/find/a.c"), "存在文件路径"
        );
        MKValue args[2] = {path, path};
        MKValue out;

        CC_CHECK(
            call_builtin(
                registry, "file_exists", args, 1, &out, &diagnostic
            ),
            "file_exists 调用"
        );
        CC_CHECK(out.type == MK_VALUE_BOOL && out.as.boolean, "文件存在");
        mk_value_clear(&out);

        CC_CHECK(
            call_builtin(
                registry, "file_mtime", args, 1, &out, &diagnostic
            ),
            "file_mtime 调用"
        );
        CC_CHECK(
            out.type == MK_VALUE_INT && out.as.integer >= 0,
            "文件时间戳"
        );
        mk_value_clear(&out);

        CC_CHECK(
            call_builtin(
                registry, "file_newer", args, 2, &out, &diagnostic
            ),
            "file_newer 调用"
        );
        CC_CHECK(
            out.type == MK_VALUE_BOOL && !out.as.boolean,
            "相同文件不更新"
        );
        mk_value_clear(&out);

        MKValue missing;
        CC_CHECK(
            make_string(&missing, "tests/data/mk/find/nope.c"),
            "缺失文件路径"
        );
        args[0] = missing;
        CC_CHECK(
            call_builtin(
                registry, "file_exists", args, 1, &out, &diagnostic
            ),
            "file_exists 缺失调用"
        );
        CC_CHECK(
            out.type == MK_VALUE_BOOL && !out.as.boolean, "文件不存在"
        );
        mk_value_clear(&out);

        CC_CHECK(
            call_builtin(
                registry, "file_mtime", args, 1, &out, &diagnostic
            ),
            "file_mtime 缺失调用"
        );
        CC_CHECK_EQ_I64("缺失文件时间戳", -1, out.as.integer);
        mk_value_clear(&out);

        mk_value_clear(&path);
        mk_value_clear(&missing);
        mk_diagnostic_clear(&diagnostic);
        mk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();
}
