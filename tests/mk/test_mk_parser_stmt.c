/*
 * @Description: MK 语句 Parser 测试：变量、赋值、块、if/while/for、return 与 break/continue。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"
#include "mk_parser_test_util.h"

#include <stdio.h>
#include <string.h>

static MKASTNodeId parse_body(
    MKTestParse *ctx, const char *body, MKASTNodeId *out_func
)
{
    char source[4096];
    int n = snprintf(
        source, sizeof(source),
        "func test() -> void {\n%s\n}\n", body
    );
    if (n < 0 || (size_t)n >= sizeof(source))
        return MK_AST_NODE_NIL;
    if (!mk_test_parse_begin(ctx, source, NULL))
        return MK_AST_NODE_NIL;

    MKASTNodeId func = mk_test_first_module_item(ctx);
    const MKASTNode *func_node = mk_test_node(ctx, func);
    if (!func_node || func_node->kind != MK_AST_FUNC_DECL)
        return MK_AST_NODE_NIL;
    if (out_func)
        *out_func = func;
    return mk_test_child(
        ctx, func, func_node->as.func_decl.param_count + 1
    );
}

static CCBOOL node_kind_is(
    const MKTestParse *ctx, MKASTNodeId id, MKASTKind kind
)
{
    const MKASTNode *node = mk_test_node(ctx, id);
    return (node && node->kind == kind) ? CCTRUE : CCFALSE;
}

static void check_body_error(const char *body, const char *expected)
{
    MKTestParse ctx;
    CC_CHECK(
        !mk_test_parse_begin(&ctx, body, NULL),
        "源码应解析失败"
    );
    CC_CHECK(ctx.ast == NULL, "失败不发布 AST");
    const MKDiagnostic *diag = mk_parser_diagnostic(&ctx.parser);
    CC_CHECK(
        diag && diag->message &&
        strstr(diag->message, expected) != NULL,
        "诊断匹配"
    );
    mk_test_parse_end(&ctx);
}

void test_mk_parser_stmt(void)
{
    /* ------------------------------------------------------------------
     * STMT-001 变量声明与赋值
     * ---------------------------------------------------------------- */
    CC_CASE("STMT-001", "变量声明与赋值");
    {
        MKTestParse ctx;
        MKASTNodeId func = MK_AST_NODE_NIL;
        MKASTNodeId block = parse_body(
            &ctx, "int x = 1; x += 2; _ = f();", &func
        );
        CC_CHECK(
            node_kind_is(&ctx, block, MK_AST_BLOCK), "函数体块"
        );
        CC_CHECK_EQ_I64(
            "块项数", 3, (CCINT64)mk_test_child_count(&ctx, block)
        );

        MKASTNodeId var = mk_test_child(&ctx, block, 0);
        CC_CHECK(node_kind_is(&ctx, var, MK_AST_VAR_DECL), "变量声明");
        const MKASTNode *var_node = mk_test_node(&ctx, var);
        CC_CHECK(
            var_node && var_node->as.var_decl.is_const == CCFALSE,
            "非常量"
        );
        CC_CHECK_EQ_I64(
            "变量声明子节点数", 2, (CCINT64)mk_test_child_count(&ctx, var)
        );

        MKASTNodeId add = mk_test_child(&ctx, block, 1);
        CC_CHECK(node_kind_is(&ctx, add, MK_AST_ASSIGN), "+= 赋值");
        const MKASTNode *add_node = mk_test_node(&ctx, add);
        CC_CHECK(
            add_node && add_node->as.assign.op == MK_ASSIGN_ADD,
            "赋值运算符 += "
        );
        CC_CHECK(
            node_kind_is(&ctx, mk_test_child(&ctx, add, 0), MK_AST_IDENT),
            "赋值目标是标识符"
        );

        MKASTNodeId discard = mk_test_child(&ctx, block, 2);
        CC_CHECK(node_kind_is(&ctx, discard, MK_AST_ASSIGN), "_ 赋值");
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, discard, 0), MK_AST_DISCARD
            ),
            "丢弃目标"
        );
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, discard, 1), MK_AST_CALL
            ),
            "赋值值"
        );
        mk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STMT-002 if / else if
     * ---------------------------------------------------------------- */
    CC_CASE("STMT-002", "if / else if");
    {
        MKTestParse ctx;
        MKASTNodeId block = parse_body(
            &ctx,
            "if (a) { int x = 1; } else if (b) { x = 2; }",
            NULL
        );
        CC_CHECK_EQ_I64(
            "块项数", 1, (CCINT64)mk_test_child_count(&ctx, block)
        );
        MKASTNodeId outer = mk_test_child(&ctx, block, 0);
        CC_CHECK(node_kind_is(&ctx, outer, MK_AST_IF), "外层 if");
        const MKASTNode *outer_node = mk_test_node(&ctx, outer);
        CC_CHECK(
            outer_node && outer_node->as.if_stmt.has_else == CCTRUE,
            "外层有 else"
        );
        CC_CHECK_EQ_I64(
            "if 子节点数", 3, (CCINT64)mk_test_child_count(&ctx, outer)
        );
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, outer, 1), MK_AST_BLOCK
            ),
            "then 块"
        );

        MKASTNodeId inner = mk_test_child(&ctx, outer, 2);
        CC_CHECK(node_kind_is(&ctx, inner, MK_AST_IF), "else if");
        const MKASTNode *inner_node = mk_test_node(&ctx, inner);
        CC_CHECK(
            inner_node && inner_node->as.if_stmt.has_else == CCFALSE,
            "else if 无 else"
        );
        mk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STMT-003 while 与 for-in
     * ---------------------------------------------------------------- */
    CC_CASE("STMT-003", "while 与 for-in");
    {
        MKTestParse ctx;
        MKASTNodeId block = parse_body(
            &ctx,
            "while (a < b) { a += 1; }\n"
            "for (string s in @{sources}) { info(s); }",
            NULL
        );
        CC_CHECK_EQ_I64(
            "块项数", 2, (CCINT64)mk_test_child_count(&ctx, block)
        );

        MKASTNodeId loop = mk_test_child(&ctx, block, 0);
        CC_CHECK(node_kind_is(&ctx, loop, MK_AST_WHILE), "while 节点");
        CC_CHECK_EQ_I64(
            "while 子节点数", 2, (CCINT64)mk_test_child_count(&ctx, loop)
        );
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, loop, 0), MK_AST_BINARY
            ),
            "while 条件"
        );
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, loop, 1), MK_AST_BLOCK
            ),
            "while 循环体"
        );

        MKASTNodeId for_node = mk_test_child(&ctx, block, 1);
        CC_CHECK(node_kind_is(&ctx, for_node, MK_AST_FOR_IN), "for-in 节点");
        CC_CHECK_EQ_I64(
            "for-in 子节点数", 3,
            (CCINT64)mk_test_child_count(&ctx, for_node)
        );
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, for_node, 0), MK_AST_TYPE
            ),
            "for-in 元素类型"
        );
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, for_node, 1),
                MK_AST_CONFIG_REF
            ),
            "for-in 可迭代对象"
        );
        const MKASTNode *for_node_data = mk_test_node(&ctx, for_node);
        if (for_node_data)
        {
            CCUINT32 len = 0;
            const char *name = mk_ast_string(
                ctx.ast, for_node_data->as.for_in.name, &len
            );
            CC_CHECK_EQ_STR("for-in 变量名", "s", name);
        }
        mk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STMT-004 return / break / continue
     * ---------------------------------------------------------------- */
    CC_CASE("STMT-004", "return / break / continue");
    {
        MKTestParse ctx;
        MKASTNodeId block = parse_body(
            &ctx,
            "while (a) { if (b) { break; } else { continue; } }\n"
            "return;",
            NULL
        );
        CC_CHECK_EQ_I64(
            "块项数", 2, (CCINT64)mk_test_child_count(&ctx, block)
        );
        MKASTNodeId while_node = mk_test_child(&ctx, block, 0);
        MKASTNodeId while_body = mk_test_child(&ctx, while_node, 1);
        MKASTNodeId if_node = mk_test_child(&ctx, while_body, 0);
        CC_CHECK(node_kind_is(&ctx, if_node, MK_AST_IF), "循环内 if");
        MKASTNodeId then_block = mk_test_child(&ctx, if_node, 1);
        MKASTNodeId else_block = mk_test_child(&ctx, if_node, 2);
        CC_CHECK(
            node_kind_is(&ctx, mk_test_child(&ctx, then_block, 0), MK_AST_BREAK),
            "break 节点"
        );
        CC_CHECK(
            node_kind_is(
                &ctx, mk_test_child(&ctx, else_block, 0), MK_AST_CONTINUE
            ),
            "continue 节点"
        );

        MKASTNodeId ret = mk_test_child(&ctx, block, 1);
        CC_CHECK(node_kind_is(&ctx, ret, MK_AST_RETURN), "return 节点");
        const MKASTNode *ret_node = mk_test_node(&ctx, ret);
        CC_CHECK(
            ret_node && ret_node->as.return_stmt.has_value == CCFALSE,
            "return 无值"
        );
        CC_CHECK_EQ_I64(
            "return 子节点数", 0, (CCINT64)mk_test_child_count(&ctx, ret)
        );
        mk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STMT-005 悬空 else
     * ---------------------------------------------------------------- */
    CC_CASE("STMT-005", "悬空 else");
    {
        MKTestParse ctx;
        MKASTNodeId block = parse_body(
            &ctx,
            "if (a) if (b) { x = 1; } else { x = 2; }",
            NULL
        );
        MKASTNodeId outer = mk_test_child(&ctx, block, 0);
        const MKASTNode *outer_node = mk_test_node(&ctx, outer);
        CC_CHECK(
            outer_node && outer_node->as.if_stmt.has_else == CCFALSE,
            "外层 if 无 else"
        );
        MKASTNodeId inner = mk_test_child(&ctx, outer, 1);
        const MKASTNode *inner_node = mk_test_node(&ctx, inner);
        CC_CHECK(
            inner_node && inner_node->as.if_stmt.has_else == CCTRUE,
            "else 绑定内层 if"
        );
        mk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STMT-006 表达式语句与 const
     * ---------------------------------------------------------------- */
    CC_CASE("STMT-006", "表达式语句与 const");
    {
        MKTestParse ctx;
        MKASTNodeId block = parse_body(
            &ctx,
            "const int x = 1; 1 + 2; f(); @{value};",
            NULL
        );
        CC_CHECK_EQ_I64(
            "块项数", 4, (CCINT64)mk_test_child_count(&ctx, block)
        );
        MKASTNodeId var = mk_test_child(&ctx, block, 0);
        const MKASTNode *var_node = mk_test_node(&ctx, var);
        CC_CHECK(
            var_node && var_node->as.var_decl.is_const == CCTRUE,
            "const 变量"
        );
        CC_CHECK(
            node_kind_is(&ctx, mk_test_child(&ctx, block, 1), MK_AST_EXPR_STMT),
            "二元表达式语句"
        );
        CC_CHECK(
            node_kind_is(&ctx, mk_test_child(&ctx, block, 2), MK_AST_EXPR_STMT),
            "调用表达式语句"
        );
        CC_CHECK(
            node_kind_is(&ctx, mk_test_child(&ctx, block, 3), MK_AST_EXPR_STMT),
            "配置引用语句"
        );
        mk_test_parse_end(&ctx);
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * STMT-007 语法错误
     * ---------------------------------------------------------------- */
    CC_CASE("STMT-007", "语法错误");
    {
        check_body_error(
            "func bad() -> void { if (a) int x = 1; }",
            "期望表达式"
        );
        check_body_error(
            "func bad() -> void { for (string s in xs) int x = 1; }",
            "期望表达式"
        );
        check_body_error(
            "func bad() -> void { x = y = 1; }",
            "分号"
        );
        check_body_error(
            "func bad() -> void { _ += 1; }",
            "`_` 不能使用"
        );
        check_body_error(
            "func bad() -> void { if (x = 1) {} }",
            "右圆括号"
        );
        check_body_error(
            "func bad() -> void { while (x = 1) {} }",
            "右圆括号"
        );
        check_body_error(
            "func bad() -> void { if (a) {} else; }",
            "期望表达式"
        );
    }
    CC_CASE_END();
}
