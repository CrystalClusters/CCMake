/*
 * @Description: CMK 内建注册表与标准内建测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_builtin.h"

#include <string.h>

static CCBOOL make_string(CMKValue *value, const char *text)
{
    cmk_value_init(value);
    return cmk_value_set_string(value, text, (CCUINT32)strlen(text));
}

static CCBOOL make_int(CMKValue *value, CCINT64 integer)
{
    cmk_value_init(value);
    return cmk_value_set_int(value, integer);
}

static CCBOOL make_string_list(
    CMKValue *value, const char *const *items, CCUINT32 count
)
{
    cmk_value_init(value);
    if (!cmk_value_set_list(value, CMK_VALUE_STRING))
        return CCFALSE;
    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKValue item;
        if (!make_string(&item, items[i]))
            return CCFALSE;
        CCBOOL ok = cmk_list_append(value->as.list, &item);
        cmk_value_clear(&item);
        if (!ok)
            return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL call_builtin(
    const CMKBuiltinRegistry *registry, const char *name,
    CMKValue *args, CCUINT32 argc, CMKValue *out,
    CMKDiagnostic *diagnostic
)
{
    const CMKBuiltinSpec *spec = cmk_builtin_find(
        registry, name, (CCUINT32)strlen(name)
    );
    if (!spec)
        return CCFALSE;

    cmk_diagnostic_clear(diagnostic);
    cmk_value_init(out);
    CMKBuiltinCall call;
    memset(&call, 0, sizeof(call));
    call.runtime = NULL;
    call.args = args;
    call.argc = argc;
    call.out = out;
    call.diagnostic = diagnostic;
    call.file = "builtin.cmk";
    call.line = 1;
    call.col = 1;
    return spec->fn(&call);
}

void test_cmk_builtin(void)
{
    /* ------------------------------------------------------------------
     * BLT-001 注册表
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-001", "注册表");
    {
        CMKBuiltinRegistry *registry = cmk_builtin_registry_create();
        CC_CHECK(registry != NULL, "创建注册表");
        CC_CHECK(cmk_builtin_register_standard(registry), "注册标准内建");
        CC_CHECK(
            cmk_builtin_find(registry, "len", 3) != NULL,
            "查找 len"
        );
        CC_CHECK(
            cmk_builtin_find(registry, "command", 7) != NULL,
            "查找 command"
        );
        CC_CHECK(
            cmk_builtin_find(registry, "missing", 7) == NULL,
            "未知内建"
        );
        CC_CHECK(
            !cmk_builtin_register_standard(registry),
            "重复注册失败"
        );

        CMKBuiltinSpec invalid;
        memset(&invalid, 0, sizeof(invalid));
        invalid.abi_version = CMK_BUILTIN_ABI_VERSION + 1;
        invalid.name = "bad";
        CC_CHECK(!cmk_builtin_register(registry, &invalid), "ABI 不匹配");

        cmk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-002 纯计算与查询
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-002", "纯计算与查询");
    {
        CMKBuiltinRegistry *registry = cmk_builtin_registry_create();
        CC_CHECK(cmk_builtin_register_standard(registry), "注册标准内建");
        CMKDiagnostic diagnostic;
        cmk_diagnostic_init(&diagnostic);

        const char *items[] = {"a", "b", "c"};
        CMKValue list;
        CC_CHECK(make_string_list(&list, items, 3), "构造字符串列表");
        CMKValue args[3];
        CMKValue out;
        args[0] = list;
        CC_CHECK(
            call_builtin(registry, "len", args, 1, &out, &diagnostic),
            "len 调用"
        );
        CC_CHECK_EQ_I64("len 结果", 3, out.as.integer);
        cmk_value_clear(&out);

        CMKValue sep;
        CC_CHECK(make_string(&sep, ", "), "分隔符");
        args[0] = list;
        args[1] = sep;
        CC_CHECK(
            call_builtin(registry, "join", args, 2, &out, &diagnostic),
            "join 调用"
        );
        CC_CHECK_EQ_STR("join 结果", "a, b, c", out.as.string.data);
        cmk_value_clear(&out);

        CMKValue text;
        CMKValue from;
        CMKValue to;
        CC_CHECK(make_string(&text, "src/main.c"), "subst 输入");
        CC_CHECK(make_string(&from, ".c"), "subst from");
        CC_CHECK(make_string(&to, ".o"), "subst to");
        args[0] = text;
        args[1] = from;
        args[2] = to;
        CC_CHECK(
            call_builtin(registry, "subst", args, 3, &out, &diagnostic),
            "subst 调用"
        );
        CC_CHECK_EQ_STR("subst 结果", "src/main.o", out.as.string.data);
        cmk_value_clear(&out);

        args[0] = text;
        CC_CHECK(
            call_builtin(registry, "basename", args, 1, &out, &diagnostic),
            "basename 调用"
        );
        CC_CHECK_EQ_STR("basename 结果", "main.c", out.as.string.data);
        cmk_value_clear(&out);

        CC_CHECK(
            call_builtin(registry, "dirname", args, 1, &out, &diagnostic),
            "dirname 调用"
        );
        CC_CHECK_EQ_STR("dirname 结果", "src", out.as.string.data);
        cmk_value_clear(&out);

        CMKValue key;
        CC_CHECK(make_string(&key, "os"), "host_info 键");
        args[0] = key;
        CC_CHECK(
            call_builtin(registry, "host_info", args, 1, &out, &diagnostic),
            "host_info 调用"
        );
        CC_CHECK(out.type == CMK_VALUE_STRING, "host_info 返回字符串");
        cmk_value_clear(&out);

        cmk_value_clear(&key);
        CC_CHECK(make_string(&key, "nope"), "非法 host_info 键");
        args[0] = key;
        CC_CHECK(
            !call_builtin(registry, "host_info", args, 1, &out, &diagnostic),
            "非法 host_info 键失败"
        );
        cmk_value_clear(&out);

        cmk_value_clear(&list);
        cmk_value_clear(&sep);
        cmk_value_clear(&text);
        cmk_value_clear(&from);
        cmk_value_clear(&to);
        cmk_value_clear(&key);
        cmk_diagnostic_clear(&diagnostic);
        cmk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-003 动作构造
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-003", "动作构造");
    {
        CMKBuiltinRegistry *registry = cmk_builtin_registry_create();
        CC_CHECK(cmk_builtin_register_standard(registry), "注册标准内建");
        CMKDiagnostic diagnostic;
        cmk_diagnostic_init(&diagnostic);

        const char *flags[] = {"-O2", "a.c"};
        CMKValue program;
        CMKValue argv;
        CMKValue args[2];
        CMKValue out;
        CC_CHECK(make_string(&program, "cc"), "程序名");
        CC_CHECK(make_string_list(&argv, flags, 2), "参数列表");
        args[0] = program;
        args[1] = argv;
        CC_CHECK(
            call_builtin(registry, "command", args, 2, &out, &diagnostic),
            "command 调用"
        );
        CC_CHECK(
            out.type == CMK_VALUE_ACTION &&
            out.as.action->kind == CMK_ACTION_EXEC &&
            strcmp(out.as.action->program.data, "cc") == 0 &&
            cmk_list_count(out.as.action->args) == 2,
            "EXEC 动作"
        );
        cmk_value_clear(&out);

        cmk_value_clear(&program);
        CC_CHECK(make_string(&program, "build"), "mkdir 路径");
        args[0] = program;
        CC_CHECK(
            call_builtin(registry, "mkdir", args, 1, &out, &diagnostic),
            "mkdir 调用"
        );
        CC_CHECK(
            out.type == CMK_VALUE_ACTION &&
            out.as.action->kind == CMK_ACTION_MKDIR &&
            strcmp(out.as.action->path.data, "build") == 0,
            "MKDIR 动作"
        );
        cmk_value_clear(&out);

        cmk_value_clear(&argv);
        CC_CHECK(make_string(&argv, "from"), "copy 源");
        CMKValue to_value;
        CC_CHECK(make_string(&to_value, "to"), "copy 目标");
        args[0] = program;
        args[1] = to_value;
        CC_CHECK(
            call_builtin(registry, "copy", args, 2, &out, &diagnostic),
            "copy 调用"
        );
        CC_CHECK(
            out.type == CMK_VALUE_ACTION &&
            out.as.action->kind == CMK_ACTION_COPY,
            "COPY 动作"
        );
        cmk_value_clear(&out);

        cmk_value_clear(&program);
        cmk_value_clear(&argv);
        cmk_value_clear(&to_value);
        cmk_diagnostic_clear(&diagnostic);
        cmk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-004 错误与日志
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-004", "错误与日志");
    {
        CMKBuiltinRegistry *registry = cmk_builtin_registry_create();
        CC_CHECK(cmk_builtin_register_standard(registry), "注册标准内建");
        CMKDiagnostic diagnostic;
        cmk_diagnostic_init(&diagnostic);

        CMKValue args[2];
        CMKValue out;
        CC_CHECK(make_string(&args[0], "value=%d"), "格式串");
        CC_CHECK(make_int(&args[1], 7), "格式参数");
        CC_CHECK(
            !call_builtin(registry, "error", args, 2, &out, &diagnostic),
            "error 返回失败"
        );
        CC_CHECK(
            diagnostic.message &&
            strstr(diagnostic.message, "value=7") != NULL,
            "error 格式文本"
        );
        cmk_value_clear(&out);

        cmk_value_clear(&args[0]);
        CC_CHECK(make_string(&args[0], "only"), "单参数");
        CC_CHECK(
            !call_builtin(registry, "command", args, 1, &out, &diagnostic),
            "command 参数不足"
        );
        cmk_value_clear(&out);

        cmk_value_clear(&args[0]);
        cmk_value_clear(&args[1]);
        cmk_diagnostic_clear(&diagnostic);
        cmk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * BLT-005 find_files
     * ---------------------------------------------------------------- */
    CC_CASE("BLT-005", "find_files");
    {
        CMKBuiltinRegistry *registry = cmk_builtin_registry_create();
        CC_CHECK(cmk_builtin_register_standard(registry), "注册标准内建");
        CMKDiagnostic diagnostic;
        cmk_diagnostic_init(&diagnostic);

        CMKValue pattern;
        CC_CHECK(make_string(&pattern, "*.c"), "find pattern");
        const char *roots[] = {"tests/data/cmk/find"};
        CMKValue root_list;
        CC_CHECK(make_string_list(&root_list, roots, 1), "find roots");
        CMKValue args[2] = {pattern, root_list};
        CMKValue out;
        CC_CHECK(
            call_builtin(
                registry, "find_files", args, 2, &out, &diagnostic
            ),
            "find_files 调用"
        );
        CC_CHECK_EQ_I64(
            "find_files 结果数", 3,
            (CCINT64)cmk_list_count(out.as.list)
        );
        const CMKValue *first = cmk_list_at(out.as.list, 0);
        const CMKValue *second = cmk_list_at(out.as.list, 1);
        const CMKValue *third = cmk_list_at(out.as.list, 2);
        CC_CHECK(
            first && second && third &&
            strstr(first->as.string.data, "find/a.c") != NULL &&
            strstr(second->as.string.data, "find/sub/b.c") != NULL &&
            strstr(third->as.string.data, "find/sub/deep/c.c") != NULL,
            "find_files 结果排序"
        );
        cmk_value_clear(&out);
        cmk_value_clear(&pattern);
        cmk_value_clear(&root_list);
        cmk_diagnostic_clear(&diagnostic);
        cmk_builtin_registry_destroy(registry);
    }
    CC_CASE_END();
}
