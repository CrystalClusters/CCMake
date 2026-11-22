/*
 * @Description: CMK 声明 Parser 测试：导入/包含、变量/函数、target、顶层 if 与完整模块。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "cmk_parser_test_util.h"

#include <stdio.h>
#include <string.h>

static CCBOOL node_kind_is(
    const CMKTestParse *ctx, CMKASTNodeId id, CMKASTKind kind
)
{
    const CMKASTNode *node = cmk_test_node(ctx, id);
    return (node && node->kind == kind) ? CCTRUE : CCFALSE;
}

static CCBOOL string_is(
    const CMKTestParse *ctx, CMKASTStringId id, const char *expected
)
{
    CCUINT32 len = 0;
    const char *text = cmk_ast_string(ctx->ast, id, &len);
    size_t expected_len = strlen(expected);
    return (text && len == expected_len &&
            memcmp(text, expected, len) == 0) ? CCTRUE : CCFALSE;
}

static void check_module_error(const char *src, const char *expected)
{
    CMKTestParse ctx;
    CC_CHECK(
        !cmk_test_parse_begin(&ctx, src, NULL),
        "源码应解析失败"
    );
    CC_CHECK(ctx.ast == NULL, "失败不发布 AST");
    const CMKDiagnostic *diag = cmk_parser_diagnostic(&ctx.parser);
    CC_CHECK(
        diag && diag->message &&
        strstr(diag->message, expected) != NULL,
        "诊断匹配"
    );
    cmk_test_parse_end(&ctx);
}

void test_cmk_parser_decl(void)
{
    /* ------------------------------------------------------------------
     * DECL-001 include / import
     * ---------------------------------------------------------------- */
    CC_CASE("DECL-001", "include / import");
    {
        const char *src =
            "@include \"project.ccm\";\n"
            "@import func compile, link from \"rules/compile.cmk\";\n"
            "@import func release as release_flags from \"rules/release.cmk\";\n"
            "@import func * from \"rules/all.cmk\";\n"
            "@import target core, utils from \"targets/common.cmk\" as common;\n";

        CMKTestParse ctx;
        CC_CHECK(cmk_test_parse_begin(&ctx, src, NULL), "解析导入与包含");
        CC_CHECK_EQ_I64(
            "顶层项数", 5, (CCINT64)cmk_test_child_count(
                &ctx, cmk_ast_root(ctx.ast)
            )
        );

        CMKASTNodeId include = cmk_test_first_module_item(&ctx);
        CC_CHECK(node_kind_is(&ctx, include, CMK_AST_INCLUDE_DECL), "include 节点");
        const CMKASTNode *include_node = cmk_test_node(&ctx, include);
        CC_CHECK(
            include_node &&
            string_is(&ctx, include_node->as.include_decl.module, "project.ccm"),
            "include 路径"
        );

        CMKASTNodeId func1 = cmk_test_child(
            &ctx, cmk_ast_root(ctx.ast), 1
        );
        CC_CHECK(node_kind_is(&ctx, func1, CMK_AST_IMPORT_DECL), "函数导入");
        const CMKASTNode *func1_node = cmk_test_node(&ctx, func1);
        CC_CHECK(
            func1_node && func1_node->as.import_decl.kind == CMK_IMPORT_FUNC,
            "函数导入类型"
        );
        CC_CHECK_EQ_I64(
            "函数导入项数", 2, (CCINT64)cmk_test_child_count(&ctx, func1)
        );

        CMKASTNodeId func2 = cmk_test_child(
            &ctx, cmk_ast_root(ctx.ast), 2
        );
        CMKASTNodeId item2 = cmk_test_child(&ctx, func2, 0);
        const CMKASTNode *item2_node = cmk_test_node(&ctx, item2);
        CC_CHECK(
            item2_node &&
            string_is(&ctx, item2_node->as.import_item.name, "release") &&
            string_is(&ctx, item2_node->as.import_item.alias, "release_flags"),
            "函数别名"
        );

        CMKASTNodeId func3 = cmk_test_child(
            &ctx, cmk_ast_root(ctx.ast), 3
        );
        const CMKASTNode *func3_node = cmk_test_node(&ctx, func3);
        CC_CHECK(
            func3_node && func3_node->as.import_decl.wildcard == CCTRUE,
            "通配函数导入"
        );
        CC_CHECK_EQ_I64(
            "通配导入项数", 0, (CCINT64)cmk_test_child_count(&ctx, func3)
        );

        CMKASTNodeId target = cmk_test_child(
            &ctx, cmk_ast_root(ctx.ast), 4
        );
        const CMKASTNode *target_node = cmk_test_node(&ctx, target);
        CC_CHECK(
            target_node &&
            target_node->as.import_decl.kind == CMK_IMPORT_TARGET &&
            string_is(&ctx, target_node->as.import_decl.module,
                      "targets/common.cmk") &&
            string_is(&ctx, target_node->as.import_decl.alias, "common"),
            "目标导入与别名"
        );
        CC_CHECK_EQ_I64(
            "目标导入项数", 2, (CCINT64)cmk_test_child_count(&ctx, target)
        );
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * DECL-002 变量与函数声明
     * ---------------------------------------------------------------- */
    CC_CASE("DECL-002", "变量与函数声明");
    {
        const char *src =
            "const string name = \"app\";\n"
            "int count = 1 + 2;\n"
            "func build(string mode) -> int {\n"
            "    int x = 1;\n"
            "    return x;\n"
            "}\n"
            "func log(string msg) -> void { info(msg); }\n";

        CMKTestParse ctx;
        CC_CHECK(cmk_test_parse_begin(&ctx, src, NULL), "解析声明");
        CC_CHECK_EQ_I64(
            "顶层项数", 4,
            (CCINT64)cmk_test_child_count(&ctx, cmk_ast_root(ctx.ast))
        );

        CMKASTNodeId name_var = cmk_test_first_module_item(&ctx);
        const CMKASTNode *name_node = cmk_test_node(&ctx, name_var);
        CC_CHECK(
            name_node && name_node->as.var_decl.is_const == CCTRUE,
            "const 变量"
        );
        CC_CHECK(
            string_is(&ctx, name_node->as.var_decl.name, "name"),
            "变量名"
        );

        CMKASTNodeId count_var = cmk_test_child(
            &ctx, cmk_ast_root(ctx.ast), 1
        );
        CMKASTNodeId init = cmk_test_child(&ctx, count_var, 1);
        CC_CHECK(node_kind_is(&ctx, init, CMK_AST_BINARY), "变量初始化表达式");

        CMKASTNodeId build = cmk_test_child(
            &ctx, cmk_ast_root(ctx.ast), 2
        );
        const CMKASTNode *build_node = cmk_test_node(&ctx, build);
        CC_CHECK(
            build_node && build_node->as.func_decl.param_count == 1,
            "函数参数数量"
        );
        CC_CHECK_EQ_I64(
            "函数子节点数", 3, (CCINT64)cmk_test_child_count(&ctx, build)
        );
        CC_CHECK(
            node_kind_is(&ctx, cmk_test_child(&ctx, build, 0), CMK_AST_TYPE),
            "函数返回类型"
        );
        CC_CHECK(
            node_kind_is(&ctx, cmk_test_child(&ctx, build, 1), CMK_AST_PARAM),
            "函数参数"
        );
        CC_CHECK(
            node_kind_is(&ctx, cmk_test_child(&ctx, build, 2), CMK_AST_BLOCK),
            "函数体"
        );

        CMKASTNodeId log_func = cmk_test_child(
            &ctx, cmk_ast_root(ctx.ast), 3
        );
        CMKASTNodeId log_type = cmk_test_child(&ctx, log_func, 0);
        const CMKASTNode *log_type_node = cmk_test_node(&ctx, log_type);
        CC_CHECK(
            log_type_node &&
            log_type_node->as.type.kind == CMK_TYPE_VOID,
            "void 返回类型"
        );
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * DECL-003 target 字段
     * ---------------------------------------------------------------- */
    CC_CASE("DECL-003", "target 字段");
    {
        const char *src =
            "target app() {\n"
            "    list<target_ref> depends_on = {core};\n"
            "    list<string> inputs = {\"a.c\"};\n"
            "    list<string> outputs = {\"build/app\"};\n"
            "    list<action> actions = {\n"
            "        mkdir(\"build\"),\n"
            "        command(\"cc\", {\"-O2\", \"a.c\"})\n"
            "    };\n"
            "}\n";

        CMKTestParse ctx;
        CC_CHECK(cmk_test_parse_begin(&ctx, src, NULL), "解析 target");
        CMKASTNodeId target = cmk_test_first_module_item(&ctx);
        CC_CHECK(node_kind_is(&ctx, target, CMK_AST_TARGET_DECL), "target 节点");
        const CMKASTNode *target_node = cmk_test_node(&ctx, target);
        CC_CHECK(
            target_node &&
            string_is(&ctx, target_node->as.target_decl.name, "app"),
            "target 名"
        );
        CC_CHECK_EQ_I64(
            "字段数", 4, (CCINT64)cmk_test_child_count(&ctx, target)
        );

        CMKASTNodeId depends = cmk_test_child(&ctx, target, 0);
        const CMKASTNode *depends_node = cmk_test_node(&ctx, depends);
        CC_CHECK(
            depends_node &&
            depends_node->as.target_field.field == CMK_TARGET_FIELD_DEPENDS_ON,
            "depends_on 字段"
        );
        CC_CHECK(
            string_is(&ctx, depends_node->as.target_field.name, "depends_on"),
            "depends_on 字段名"
        );

        CMKASTNodeId actions = cmk_test_child(&ctx, target, 3);
        CC_CHECK_EQ_I64(
            "action 列表元素数", 2,
            (CCINT64)cmk_test_child_count(&ctx, cmk_test_child(&ctx, actions, 1))
        );
        cmk_test_parse_end(&ctx);

        CC_CHECK(
            cmk_test_parse_begin(
                &ctx,
                "target app() { list<string> unknown_field = {\"x\"}; }",
                NULL
            ),
            "未知 target 字段语法可解析"
        );
        CMKASTNodeId unknown_target = cmk_test_first_module_item(&ctx);
        CMKASTNodeId unknown_field = cmk_test_child(&ctx, unknown_target, 0);
        const CMKASTNode *unknown_node = cmk_test_node(&ctx, unknown_field);
        CC_CHECK(
            unknown_node &&
            unknown_node->as.target_field.field == CMK_TARGET_FIELD_UNKNOWN,
            "未知字段保留 UNKNOWN"
        );
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * DECL-004 顶层 if
     * ---------------------------------------------------------------- */
    CC_CASE("DECL-004", "顶层 if");
    {
        const char *src =
            "if (@{platform} == \"windows\") {\n"
            "    target app() { list<string> outputs = {\"a.exe\"}; }\n"
            "} else {\n"
            "    target app() { list<string> outputs = {\"a\"}; }\n"
            "}\n";

        CMKTestParse ctx;
        CC_CHECK(cmk_test_parse_begin(&ctx, src, NULL), "解析顶层 if");
        CMKASTNodeId if_node = cmk_test_first_module_item(&ctx);
        CC_CHECK(node_kind_is(&ctx, if_node, CMK_AST_IF), "顶层 if 节点");
        const CMKASTNode *if_data = cmk_test_node(&ctx, if_node);
        CC_CHECK(
            if_data && if_data->as.if_stmt.has_else == CCTRUE,
            "顶层 if 有 else"
        );
        CC_CHECK_EQ_I64(
            "顶层 if 子节点数", 3, (CCINT64)cmk_test_child_count(&ctx, if_node)
        );
        CMKASTNodeId then_block = cmk_test_child(&ctx, if_node, 1);
        CMKASTNodeId else_block = cmk_test_child(&ctx, if_node, 2);
        CC_CHECK(
            node_kind_is(
                &ctx, cmk_test_child(&ctx, then_block, 0), CMK_AST_TARGET_DECL
            ),
            "then 分支 target"
        );
        CC_CHECK(
            node_kind_is(
                &ctx, cmk_test_child(&ctx, else_block, 0), CMK_AST_TARGET_DECL
            ),
            "else 分支 target"
        );
        cmk_test_parse_end(&ctx);

        CC_CHECK(
            cmk_test_parse_begin(
                &ctx,
                "if (a) target app() {} else target other() {}",
                NULL
            ),
            "无花括号顶层 if"
        );
        cmk_test_parse_end(&ctx);

        CC_CHECK(
            cmk_test_parse_begin(
                &ctx,
                "if (a) { if (b) { target app() {} } }",
                NULL
            ),
            "嵌套顶层 if"
        );
        CMKASTNodeId outer_if = cmk_test_first_module_item(&ctx);
        CMKASTNodeId outer_block = cmk_test_child(&ctx, outer_if, 1);
        CC_CHECK(
            node_kind_is(
                &ctx, cmk_test_child(&ctx, outer_block, 0), CMK_AST_IF
            ),
            "嵌套顶层 if 节点"
        );
        cmk_test_parse_end(&ctx);

        check_module_error(
            "if (a) { func f() -> void {} }",
            "顶层 if 分支只允许 target"
        );
        check_module_error(
            "if (a) { int x = 1; }",
            "顶层 if 分支只允许 target"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * DECL-005 完整模块
     * ---------------------------------------------------------------- */
    CC_CASE("DECL-005", "完整模块");
    {
        const char *src =
            "@include \"project.ccm\";\n"
            "func object_of(string src) -> string {\n"
            "    return subst(src, \".c\", \".o\");\n"
            "}\n"
            "target main() {\n"
            "    list<target_ref> depends_on = {app};\n"
            "}\n"
            "target app() {\n"
            "    list<string> outputs = {\"build/app\"};\n"
            "    list<action> actions = {\n"
            "        mkdir(\"build\"),\n"
            "        command(@{toolchain.c}, @{cflags} + {\"-c\", \"src/main.c\", \"-o\", \"build/app\"})\n"
            "    };\n"
            "}\n";

        CMKTestParse ctx;
        CC_CHECK(cmk_test_parse_begin(&ctx, src, NULL), "解析完整模块");
        CC_CHECK_EQ_I64(
            "顶层项数", 4,
            (CCINT64)cmk_test_child_count(&ctx, cmk_ast_root(ctx.ast))
        );
        const char *reason = NULL;
        CC_CHECK(cmk_ast_validate(ctx.ast, &reason), "完整模块 AST 校验");
        CC_CHECK(reason == NULL, "完整模块无校验原因");
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * DECL-006 顶层与声明错误
     * ---------------------------------------------------------------- */
    CC_CASE("DECL-006", "顶层与声明错误");
    {
        check_module_error(";", "期望顶层声明");
        check_module_error(
            "func outer() -> void { func inner() -> void {} }",
            "期望表达式"
        );
        check_module_error(
            "func outer() -> void { target inner() {} }",
            "期望表达式"
        );
        check_module_error(
            "func outer() -> void { @import func a from \"x\"; }",
            "期望表达式"
        );
        check_module_error(
            "target app() { if (a) {} }",
            "期望类型"
        );
        check_module_error(
            "func f(string) -> void {}",
            "期望标识符"
        );
        check_module_error(
            "func f() -> void { return 1 }",
            "分号"
        );
        check_module_error(
            "target app() { list<string> outputs = {\"x\"}; ",
            "target 体未闭合"
        );
        check_module_error(
            "@import func a, b as c from \"x\";",
            "批量导入不能使用"
        );
        check_module_error(
            "@import target a from \"x\";",
            "期望`as`"
        );
        check_module_error(
            "@import func * as x from \"x\";",
            "批量导入不能使用"
        );
        check_module_error(
            "@import func from \"x\";",
            "期望标识符"
        );
        check_module_error(
            "func f(int a,) -> void {}",
            "尾逗号"
        );
        check_module_error(
            "func f() -> void",
            "左花括号"
        );
    }
    CC_CASE_END();
}
