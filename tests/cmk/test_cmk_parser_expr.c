/*
 * @Description: CMK 表达式 Parser 测试：优先级、结合性、调用/索引/成员、列表与字面量。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "cmk_parser_test_util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static CMKASTNodeId parse_expression(
    CMKTestParse *ctx, const char *expression
)
{
    char source[1024];
    int n = snprintf(
        source, sizeof(source), "int __expr = %s;", expression
    );
    if (n < 0 || (size_t)n >= sizeof(source))
        return CMK_AST_NODE_NIL;
    if (!cmk_test_parse_begin(ctx, source, NULL))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId var = cmk_test_first_module_item(ctx);
    const CMKASTNode *node = cmk_test_node(ctx, var);
    if (!node || node->kind != CMK_AST_VAR_DECL)
        return CMK_AST_NODE_NIL;
    return cmk_test_child(ctx, var, 1);
}

static CMKASTNodeId parse_statement_in_func(
    CMKTestParse *ctx, const char *statement
)
{
    char source[1024];
    int n = snprintf(
        source, sizeof(source),
        "func f() -> void { %s }", statement
    );
    if (n < 0 || (size_t)n >= sizeof(source))
        return CMK_AST_NODE_NIL;
    if (!cmk_test_parse_begin(ctx, source, NULL))
        return CMK_AST_NODE_NIL;

    CMKASTNodeId func = cmk_test_first_module_item(ctx);
    const CMKASTNode *func_node = cmk_test_node(ctx, func);
    if (!func_node || func_node->kind != CMK_AST_FUNC_DECL)
        return CMK_AST_NODE_NIL;
    CMKASTNodeId block = cmk_test_child(
        ctx, func, func_node->as.func_decl.param_count + 1
    );
    return cmk_test_child(ctx, block, 0);
}

static void check_expr_error(
    const char *expression, const char *expected
)
{
    char source[1024];
    int n = snprintf(
        source, sizeof(source), "int __expr = %s", expression
    );
    CC_CHECK(
        n > 0 && (size_t)n < sizeof(source),
        "错误用例源码长度"
    );
    CMKTestParse ctx;
    CC_CHECK(
        !cmk_test_parse_begin(&ctx, source, NULL),
        "表达式应解析失败"
    );
    CC_CHECK(ctx.ast == NULL, "失败不发布 AST");
    const CMKDiagnostic *diag = cmk_parser_diagnostic(&ctx.parser);
    CC_CHECK(
        diag && diag->message &&
        strstr(diag->message, expected) != NULL,
        "表达式诊断匹配"
    );
    cmk_test_parse_end(&ctx);
}

static void check_func_error(
    const char *statement, const char *expected
)
{
    char source[1024];
    int n = snprintf(
        source, sizeof(source),
        "func f() -> void { %s }", statement
    );
    CC_CHECK(
        n > 0 && (size_t)n < sizeof(source),
        "错误语句源码长度"
    );
    CMKTestParse ctx;
    CC_CHECK(
        !cmk_test_parse_begin(&ctx, source, NULL),
        "语句应解析失败"
    );
    CC_CHECK(ctx.ast == NULL, "失败不发布 AST");
    const CMKDiagnostic *diag = cmk_parser_diagnostic(&ctx.parser);
    CC_CHECK(
        diag && diag->message &&
        strstr(diag->message, expected) != NULL,
        "语句诊断匹配"
    );
    cmk_test_parse_end(&ctx);
}

static CCBOOL node_kind_is(
    const CMKTestParse *ctx, CMKASTNodeId id, CMKASTKind kind
)
{
    const CMKASTNode *node = cmk_test_node(ctx, id);
    return (node && node->kind == kind) ? CCTRUE : CCFALSE;
}

static CCBOOL node_binary_is(
    const CMKTestParse *ctx, CMKASTNodeId id, CMKBinaryOp op
)
{
    const CMKASTNode *node = cmk_test_node(ctx, id);
    return (node && node->kind == CMK_AST_BINARY &&
            node->as.binary.op == op) ? CCTRUE : CCFALSE;
}

static CCBOOL node_unary_is(
    const CMKTestParse *ctx, CMKASTNodeId id, CMKUnaryOp op
)
{
    const CMKASTNode *node = cmk_test_node(ctx, id);
    return (node && node->kind == CMK_AST_UNARY &&
            node->as.unary.op == op) ? CCTRUE : CCFALSE;
}

void test_cmk_parser_expr(void)
{
    /* ------------------------------------------------------------------
     * EXPR-001 字面量与列表
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-001", "字面量与列表");
    {
        CMKTestParse ctx;
        CMKASTNodeId expr = parse_expression(&ctx, "42");
        CC_CHECK(node_kind_is(&ctx, expr, CMK_AST_LITERAL), "整数节点");
        const CMKASTNode *node = cmk_test_node(&ctx, expr);
        CC_CHECK(
            node && node->as.literal.kind == CMK_LITERAL_INT &&
            node->as.literal.value.integer == 42,
            "整数值"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "3.5");
        node = cmk_test_node(&ctx, expr);
        CC_CHECK(
            node && node->as.literal.kind == CMK_LITERAL_FLOAT &&
            node->as.literal.value.floating == 3.5,
            "浮点值"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "\"hi\"");
        node = cmk_test_node(&ctx, expr);
        CC_CHECK(
            node && node->as.literal.kind == CMK_LITERAL_STRING,
            "字符串节点"
        );
        if (node && node->as.literal.kind == CMK_LITERAL_STRING)
        {
            CCUINT32 len = 0;
            const char *text = cmk_ast_string(
                ctx.ast, node->as.literal.value.string, &len
            );
            CC_CHECK_EQ_I64("字符串长度", 2, (CCINT64)len);
            CC_CHECK_EQ_STR("字符串内容", "hi", text);
        }
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "true");
        node = cmk_test_node(&ctx, expr);
        CC_CHECK(
            node && node->as.literal.kind == CMK_LITERAL_BOOL &&
            node->as.literal.value.boolean == CCTRUE,
            "布尔真值"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "{}");
        node = cmk_test_node(&ctx, expr);
        CC_CHECK(node_kind_is(&ctx, expr, CMK_AST_LIST), "空列表");
        CC_CHECK_EQ_I64("空列表元素数", 0, (CCINT64)cmk_test_child_count(&ctx, expr));
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "{1, 2, 3}");
        CC_CHECK_EQ_I64(
            "列表元素数", 3, (CCINT64)cmk_test_child_count(&ctx, expr)
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "{1, 2,}");
        CC_CHECK_EQ_I64(
            "尾逗号列表元素数", 2, (CCINT64)cmk_test_child_count(&ctx, expr)
        );
        cmk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * EXPR-002 优先级与结合性
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-002", "优先级与结合性");
    {
        CMKTestParse ctx;
        CMKASTNodeId expr = parse_expression(&ctx, "a + b * c");
        CC_CHECK(node_binary_is(&ctx, expr, CMK_BINARY_ADD), "外层加法");
        CC_CHECK(
            node_binary_is(
                &ctx, cmk_test_child(&ctx, expr, 1), CMK_BINARY_MUL
            ),
            "右侧乘法"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "a * b + c");
        CC_CHECK(node_binary_is(&ctx, expr, CMK_BINARY_ADD), "外层加法");
        CC_CHECK(
            node_binary_is(
                &ctx, cmk_test_child(&ctx, expr, 0), CMK_BINARY_MUL
            ),
            "左侧乘法"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "a - b - c");
        CC_CHECK(node_binary_is(&ctx, expr, CMK_BINARY_SUB), "外层减法");
        CC_CHECK(
            node_binary_is(
                &ctx, cmk_test_child(&ctx, expr, 0), CMK_BINARY_SUB
            ),
            "减法左结合"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "a == b == c");
        CC_CHECK(node_binary_is(&ctx, expr, CMK_BINARY_EQ), "外层相等");
        CC_CHECK(
            node_binary_is(
                &ctx, cmk_test_child(&ctx, expr, 0), CMK_BINARY_EQ
            ),
            "相等左结合"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "a == b < c");
        CC_CHECK(node_binary_is(&ctx, expr, CMK_BINARY_EQ), "外层相等");
        CC_CHECK(
            node_binary_is(
                &ctx, cmk_test_child(&ctx, expr, 1), CMK_BINARY_LT
            ),
            "相等右侧比较"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "a < b == c");
        CC_CHECK(node_binary_is(&ctx, expr, CMK_BINARY_EQ), "外层相等");
        CC_CHECK(
            node_binary_is(
                &ctx, cmk_test_child(&ctx, expr, 0), CMK_BINARY_LT
            ),
            "相等左侧比较"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "!-a");
        CC_CHECK(node_unary_is(&ctx, expr, CMK_UNARY_NOT), "外层逻辑非");
        CC_CHECK(
            node_unary_is(
                &ctx, cmk_test_child(&ctx, expr, 0), CMK_UNARY_NEG
            ),
            "嵌套一元负号"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "-a * b");
        CC_CHECK(node_binary_is(&ctx, expr, CMK_BINARY_MUL), "外层乘法");
        CC_CHECK(
            node_unary_is(
                &ctx, cmk_test_child(&ctx, expr, 0), CMK_UNARY_NEG
            ),
            "一元负号绑定"
        );
        cmk_test_parse_end(&ctx);

        check_expr_error("a < b < c;", "不支持链式比较");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * EXPR-003 调用、索引、成员
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-003", "调用、索引、成员");
    {
        CMKTestParse ctx;
        CMKASTNodeId expr = parse_expression(&ctx, "f()");
        CC_CHECK(node_kind_is(&ctx, expr, CMK_AST_CALL), "调用节点");
        const CMKASTNode *node = cmk_test_node(&ctx, expr);
        if (node)
        {
            CCUINT32 len = 0;
            const char *name = cmk_ast_string(
                ctx.ast, node->as.call.callee, &len
            );
            CC_CHECK_EQ_STR("调用名", "f", name);
        }
        CC_CHECK_EQ_I64("空实参数", 0, (CCINT64)cmk_test_child_count(&ctx, expr));
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "f(a, b + 1)");
        CC_CHECK_EQ_I64(
            "实参数", 2, (CCINT64)cmk_test_child_count(&ctx, expr)
        );
        CC_CHECK(
            node_binary_is(
                &ctx, cmk_test_child(&ctx, expr, 1), CMK_BINARY_ADD
            ),
            "第二个实参是加法"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "f()[0].x");
        CC_CHECK(node_kind_is(&ctx, expr, CMK_AST_MEMBER), "外层成员");
        node = cmk_test_node(&ctx, expr);
        if (node)
        {
            CCUINT32 len = 0;
            const char *member = cmk_ast_string(
                ctx.ast, node->as.member.member, &len
            );
            CC_CHECK_EQ_STR("成员名", "x", member);
        }
        CMKASTNodeId index = cmk_test_child(&ctx, expr, 0);
        CC_CHECK(node_kind_is(&ctx, index, CMK_AST_INDEX), "索引节点");
        CC_CHECK(
            node_kind_is(&ctx, cmk_test_child(&ctx, index, 0), CMK_AST_CALL),
            "索引基是调用"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "common.core");
        CC_CHECK(node_kind_is(&ctx, expr, CMK_AST_MEMBER), "目标引用成员");
        CC_CHECK(
            node_kind_is(&ctx, cmk_test_child(&ctx, expr, 0), CMK_AST_IDENT),
            "成员基是标识符"
        );
        cmk_test_parse_end(&ctx);

        check_expr_error("(f)();", "期望");
        check_expr_error("f()();", "期望");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * EXPR-004 列表与调用错误
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-004", "列表与调用错误");
    {
        check_expr_error("f(a,);", "尾逗号");
        check_expr_error("{1 2};", "逗号");
        check_expr_error("{1, 2", "右花括号");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * EXPR-005 配置引用
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-005", "配置引用");
    {
        CMKTestParse ctx;
        CMKASTNodeId expr = parse_expression(&ctx, "@{a.b[0]}");
        CC_CHECK(
            node_kind_is(&ctx, expr, CMK_AST_CONFIG_REF),
            "配置引用节点"
        );
        const CMKASTNode *node = cmk_test_node(&ctx, expr);
        if (node)
        {
            CCUINT32 len = 0;
            const char *path = cmk_ast_string(
                ctx.ast, node->as.config_ref.path, &len
            );
            CC_CHECK_EQ_I64("配置路径长度", 6, (CCINT64)len);
            CC_CHECK_EQ_STR("配置路径", "a.b[0]", path);
        }
        cmk_test_parse_end(&ctx);

        check_expr_error("@{a..b};", "配置引用路径非法");
        check_expr_error("@{a[1x]};", "配置引用路径非法");
        check_expr_error("@{a[0]b};", "配置引用路径非法");
        check_expr_error("@{.a};", "配置引用路径非法");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * EXPR-006 数值边界
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-006", "数值边界");
    {
        CMKTestParse ctx;
        CMKASTNodeId expr = parse_expression(&ctx, "9223372036854775807");
        const CMKASTNode *node = cmk_test_node(&ctx, expr);
        CC_CHECK(
            node && node->as.literal.value.integer ==
                (CCINT64)0x7FFFFFFFFFFFFFFF,
            "INT64 最大值"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, ".5");
        node = cmk_test_node(&ctx, expr);
        CC_CHECK(
            node && node->as.literal.value.floating == 0.5,
            ".5 浮点"
        );
        cmk_test_parse_end(&ctx);

        expr = parse_expression(&ctx, "2.5E-2");
        node = cmk_test_node(&ctx, expr);
        CC_CHECK(
            node &&
            fabs(node->as.literal.value.floating - 0.025) < 1e-12,
            "指数浮点"
        );
        cmk_test_parse_end(&ctx);

        check_expr_error("9223372036854775808;", "整数超出范围");
        check_expr_error("1e309;", "浮点数超出范围");
        check_expr_error("1e-400;", "浮点数下溢");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * EXPR-007 一元与丢弃符
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-007", "一元与丢弃符");
    {
        CMKTestParse ctx;
        CMKASTNodeId expr = parse_expression(&ctx, "+1");
        CC_CHECK(node_unary_is(&ctx, expr, CMK_UNARY_POS), "一元加号节点");
        cmk_test_parse_end(&ctx);

        check_expr_error("_ + 1;", "`_` 只能作为赋值目标");
        check_func_error("_ += 1;", "`_` 不能使用");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * EXPR-008 赋值不是表达式
     * ---------------------------------------------------------------- */
    CC_CASE("EXPR-008", "赋值不是表达式");
    {
        CMKTestParse ctx;
        CMKASTNodeId stmt = parse_statement_in_func(&ctx, "a = 1;");
        CC_CHECK(node_kind_is(&ctx, stmt, CMK_AST_ASSIGN), "赋值语句节点");
        cmk_test_parse_end(&ctx);

        check_func_error("if (x = 1) {}", "右圆括号");
        check_func_error("f(x = 1);", "右圆括号");
        check_func_error("a = b = 1;", "分号");
    }
    CC_CASE_END();
}
