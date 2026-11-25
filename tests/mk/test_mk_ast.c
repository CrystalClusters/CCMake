/*
 * @Description: MK AST 模型测试：所有权、构建、遍历、校验与预算默认值。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "mk/mk_ast.h"
#include "mk/mk_limits.h"

#include <stdio.h>
#include <string.h>

static MKASTNodeId add_node(MKAST *ast, MKASTKind kind)
{
    MKSourceSpan span;
    memset(&span, 0, sizeof(span));
    return mk_ast_add_node(ast, kind, &span);
}

static MKASTStringId add_text(MKAST *ast, const char *text)
{
    return mk_ast_add_string(ast, text, (CCUINT32)strlen(text));
}

static MKAST *build_valid_var_ast(MKASTNodeId *out_var)
{
    MKAST *ast = mk_ast_create("test.mk");
    if (!ast)
        return NULL;

    MKASTStringId name = add_text(ast, "count");
    MKASTNodeId var = add_node(ast, MK_AST_VAR_DECL);
    MKASTNodeId type = add_node(ast, MK_AST_TYPE);
    MKASTNodeId literal = add_node(ast, MK_AST_LITERAL);
    if (name == MK_AST_STRING_NIL ||
        var == MK_AST_NODE_NIL ||
        type == MK_AST_NODE_NIL ||
        literal == MK_AST_NODE_NIL)
        goto Failed;

    MKASTNode *var_node = mk_ast_node_mut(ast, var);
    MKASTNode *type_node = mk_ast_node_mut(ast, type);
    MKASTNode *literal_node = mk_ast_node_mut(ast, literal);
    if (!var_node || !type_node || !literal_node)
        goto Failed;

    var_node->as.var_decl.name = name;
    var_node->as.var_decl.is_const = CCFALSE;
    type_node->as.type.kind = MK_TYPE_INT;
    literal_node->as.literal.kind = MK_LITERAL_INT;
    literal_node->as.literal.value.integer = 10;

    if (!mk_ast_append_child(ast, var, type) ||
        !mk_ast_append_child(ast, var, literal) ||
        !mk_ast_append_child(ast, ast->root, var))
        goto Failed;

    if (out_var)
        *out_var = var;
    return ast;

Failed:
    mk_ast_destroy(ast);
    return NULL;
}

static void expect_valid(MKAST *ast)
{
    const char *reason = NULL;
    CC_CHECK(mk_ast_validate(ast, &reason), "AST 校验通过");
    CC_CHECK(reason == NULL, "校验通过时无失败原因");
}

static void expect_invalid(MKAST *ast, const char *text)
{
    const char *reason = NULL;
    CC_CHECK(!mk_ast_validate(ast, &reason), "AST 校验应失败");
    CC_CHECK(reason != NULL, "校验失败原因存在");
    if (reason)
        CC_CHECK(strstr(reason, text) != NULL, "校验失败原因匹配");
}

typedef struct ast_visit_log {
    MKASTKind kinds[64];
    CCUINT32 count;
    CCUINT32 stop_at;
} ASTVisitLog;

static CCBOOL ast_visit_collect(
    const MKAST *ast, MKASTNodeId id, void *user
)
{
    ASTVisitLog *log = (ASTVisitLog *)user;
    const MKASTNode *node = mk_ast_node(ast, id);
    if (!node)
        return CCFALSE;
    if (log->count >= 64)
        return CCFALSE;
    log->kinds[log->count] = node->kind;
    log->count++;
    if (log->stop_at > 0 && log->count >= log->stop_at)
        return CCFALSE;
    return CCTRUE;
}

void test_mk_ast(void)
{
    /* ------------------------------------------------------------------
     * AST-001 生命周期与根节点
     * ---------------------------------------------------------------- */
    CC_CASE("AST-001", "生命周期与根节点");
    {
        MKAST *ast = mk_ast_create("module.mk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CC_CHECK_EQ_STR("文件路径", "module.mk", mk_ast_file(ast));
            CC_CHECK(
                mk_ast_root(ast) != MK_AST_NODE_NIL,
                "根节点存在"
            );
            CC_CHECK_EQ_I64("节点数", 1, (CCINT64)mk_ast_node_count(ast));
            CC_CHECK_EQ_I64("字符串数", 0, (CCINT64)mk_ast_string_count(ast));

            const MKASTNode *root = mk_ast_node(ast, mk_ast_root(ast));
            CC_CHECK(root != NULL, "读取根节点");
            if (root)
            {
                CC_CHECK_EQ_I64(
                    "根节点类型", (CCINT64)MK_AST_MODULE, (CCINT64)root->kind
                );
                CC_CHECK(
                    root->parent == MK_AST_NODE_NIL,
                    "根节点无父节点"
                );
                CC_CHECK(
                    root->first_child == MK_AST_NODE_NIL &&
                    root->last_child == MK_AST_NODE_NIL,
                    "根节点无子节点"
                );
            }

            expect_valid(ast);
            CC_CHECK(
                mk_ast_node(ast, 999) == NULL,
                "越界节点返回 NULL"
            );
            CC_CHECK(
                mk_ast_string(ast, 0, NULL) == NULL,
                "越界字符串返回 NULL"
            );

            MKSourceSpan span;
            memset(&span, 0, sizeof(span));
            CC_CHECK(
                mk_ast_add_node(ast, MK_AST_KIND_COUNT, &span) ==
                MK_AST_NODE_NIL,
                "非法节点类型被拒绝"
            );
            mk_ast_destroy(ast);
        }

        MKAST *anonymous = mk_ast_create(NULL);
        CC_CHECK(anonymous != NULL, "匿名 AST");
        if (anonymous)
        {
            CC_CHECK(mk_ast_file(anonymous) == NULL, "匿名 AST 无文件路径");
            mk_ast_destroy(anonymous);
        }

        mk_ast_destroy(NULL);
        CC_CHECK(mk_ast_node(NULL, 0) == NULL, "空 AST 查询节点");
        CC_CHECK(mk_ast_root(NULL) == MK_AST_NODE_NIL, "空 AST 无根节点");
        CC_CHECK(mk_ast_node_count(NULL) == 0, "空 AST 节点数为 0");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-002 文件路径与字符串所有权
     * ---------------------------------------------------------------- */
    CC_CASE("AST-002", "字符串与文件路径所有权");
    {
        char path[] = "a.mk";
        MKAST *ast = mk_ast_create(path);
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            path[0] = 'b';
            CC_CHECK_EQ_STR("文件路径被复制", "a.mk", mk_ast_file(ast));

            char data[] = { 'a', '\0', 'b' };
            MKASTStringId id = mk_ast_add_string(ast, data, 3);
            CC_CHECK(id != MK_AST_STRING_NIL, "添加字符串");
            data[0] = 'z';

            CCUINT32 len = 0;
            const char *text = mk_ast_string(ast, id, &len);
            CC_CHECK(text != NULL, "读取字符串");
            CC_CHECK_EQ_I64("字符串长度", 3, (CCINT64)len);
            CC_CHECK(
                text && text[0] == 'a' && text[1] == '\0' && text[2] == 'b',
                "字符串复制且保留 0 字节"
            );

            MKASTStringId empty = mk_ast_add_string(ast, "", 0);
            CC_CHECK(empty != MK_AST_STRING_NIL, "添加空字符串");
            text = mk_ast_string(ast, empty, &len);
            CC_CHECK(text != NULL, "读取空字符串");
            CC_CHECK_EQ_I64("空字符串长度", 0, (CCINT64)len);
            CC_CHECK(
                mk_ast_add_string(ast, NULL, 1) == MK_AST_STRING_NIL,
                "空指针非零长度被拒绝"
            );

            mk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-003 构建、链接、遍历与校验
     * ---------------------------------------------------------------- */
    CC_CASE("AST-003", "构建、链接、遍历与校验");
    {
        MKASTNodeId var = MK_AST_NODE_NIL;
        MKAST *ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建变量声明 AST");
        if (ast)
        {
            expect_valid(ast);

            const MKASTNode *root = mk_ast_node(ast, mk_ast_root(ast));
            const MKASTNode *var_node = mk_ast_node(ast, var);
            CC_CHECK(root != NULL && var_node != NULL, "读取节点");
            if (root && var_node)
            {
                CC_CHECK(
                    root->first_child == var && root->last_child == var,
                    "根节点子链接"
                );
                CC_CHECK(
                    var_node->parent == mk_ast_root(ast),
                    "变量父链接"
                );
                CC_CHECK(
                    var_node->first_child != MK_AST_NODE_NIL &&
                    var_node->last_child != MK_AST_NODE_NIL,
                    "变量子链接"
                );
            }

            ASTVisitLog log;
            memset(&log, 0, sizeof(log));
            CC_CHECK(
                mk_ast_visit(
                    ast, mk_ast_root(ast), ast_visit_collect, &log
                ),
                "遍历 AST"
            );
            CC_CHECK_EQ_I64("访问节点数", 4, (CCINT64)log.count);
            if (log.count == 4)
            {
                CC_CHECK_EQ_I64(
                    "访问 0", (CCINT64)MK_AST_MODULE,
                    (CCINT64)log.kinds[0]
                );
                CC_CHECK_EQ_I64(
                    "访问 1", (CCINT64)MK_AST_VAR_DECL,
                    (CCINT64)log.kinds[1]
                );
                CC_CHECK_EQ_I64(
                    "访问 2", (CCINT64)MK_AST_TYPE,
                    (CCINT64)log.kinds[2]
                );
                CC_CHECK_EQ_I64(
                    "访问 3", (CCINT64)MK_AST_LITERAL,
                    (CCINT64)log.kinds[3]
                );
            }

            log.count = 0;
            log.stop_at = 1;
            CC_CHECK(
                !mk_ast_visit(
                    ast, mk_ast_root(ast), ast_visit_collect, &log
                ),
                "visitor 中断"
            );
            CC_CHECK_EQ_I64("中断时只访问一个节点", 1, (CCINT64)log.count);

            mk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-004 子节点挂接规则
     * ---------------------------------------------------------------- */
    CC_CASE("AST-004", "子节点挂接规则");
    {
        MKAST *ast = mk_ast_create("links.mk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            MKASTNodeId a = add_node(ast, MK_AST_VAR_DECL);
            MKASTNodeId b = add_node(ast, MK_AST_VAR_DECL);
            if (a != MK_AST_NODE_NIL && b != MK_AST_NODE_NIL)
            {
                CC_CHECK(mk_ast_append_child(ast, a, b), "首次挂接");
                CC_CHECK(!mk_ast_append_child(ast, a, b), "拒绝重复挂接");
                CC_CHECK(!mk_ast_append_child(ast, a, a), "拒绝自挂接");
                CC_CHECK(
                    !mk_ast_append_child(ast, a, ast->root),
                    "拒绝挂接根节点"
                );
                CC_CHECK(
                    !mk_ast_append_child(ast, a, 999),
                    "拒绝越界子节点"
                );
                CC_CHECK(
                    !mk_ast_append_child(ast, b, a),
                    "拒绝形成祖先环"
                );
            }
            mk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-005 校验失败路径
     * ---------------------------------------------------------------- */
    CC_CASE("AST-005", "校验失败路径");
    {
        // 子节点数量错误
        MKAST *ast = mk_ast_create("bad-count.mk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            MKASTStringId name = add_text(ast, "x");
            MKASTNodeId var = add_node(ast, MK_AST_VAR_DECL);
            MKASTNodeId type = add_node(ast, MK_AST_TYPE);
            MKASTNode *var_node = mk_ast_node_mut(ast, var);
            MKASTNode *type_node = mk_ast_node_mut(ast, type);
            if (name != MK_AST_STRING_NIL && var_node && type_node)
            {
                var_node->as.var_decl.name = name;
                type_node->as.type.kind = MK_TYPE_INT;
                CC_CHECK(mk_ast_append_child(ast, var, type), "挂接类型");
                CC_CHECK(
                    mk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "变量声明子节点数量错误");
            }
            mk_ast_destroy(ast);
        }

        // 子节点类型错误
        ast = mk_ast_create("bad-kind.mk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            MKASTStringId name = add_text(ast, "x");
            MKASTNodeId var = add_node(ast, MK_AST_VAR_DECL);
            MKASTNodeId lit1 = add_node(ast, MK_AST_LITERAL);
            MKASTNodeId lit2 = add_node(ast, MK_AST_LITERAL);
            MKASTNode *var_node = mk_ast_node_mut(ast, var);
            if (name != MK_AST_STRING_NIL && var_node)
            {
                var_node->as.var_decl.name = name;
                CC_CHECK(mk_ast_append_child(ast, var, lit1), "挂接 lit1");
                CC_CHECK(mk_ast_append_child(ast, var, lit2), "挂接 lit2");
                CC_CHECK(
                    mk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "变量声明子节点类型错误");
            }
            mk_ast_destroy(ast);
        }

        // 字符串引用无效
        ast = mk_ast_create("bad-string.mk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            MKASTNodeId var = add_node(ast, MK_AST_VAR_DECL);
            MKASTNodeId type = add_node(ast, MK_AST_TYPE);
            MKASTNodeId literal = add_node(ast, MK_AST_LITERAL);
            MKASTNode *var_node = mk_ast_node_mut(ast, var);
            MKASTNode *type_node = mk_ast_node_mut(ast, type);
            MKASTNode *literal_node = mk_ast_node_mut(ast, literal);
            if (var_node && type_node && literal_node)
            {
                var_node->as.var_decl.name = MK_AST_STRING_NIL;
                type_node->as.type.kind = MK_TYPE_INT;
                literal_node->as.literal.kind = MK_LITERAL_INT;
                CC_CHECK(mk_ast_append_child(ast, var, type), "挂接类型");
                CC_CHECK(
                    mk_ast_append_child(ast, var, literal),
                    "挂接字面量"
                );
                CC_CHECK(
                    mk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "变量名无效");
            }
            mk_ast_destroy(ast);
        }

        // list 类型缺少元素类型
        ast = mk_ast_create("bad-list.mk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            MKASTStringId name = add_text(ast, "x");
            MKASTNodeId var = add_node(ast, MK_AST_VAR_DECL);
            MKASTNodeId type = add_node(ast, MK_AST_TYPE);
            MKASTNodeId literal = add_node(ast, MK_AST_LITERAL);
            MKASTNode *var_node = mk_ast_node_mut(ast, var);
            MKASTNode *type_node = mk_ast_node_mut(ast, type);
            MKASTNode *literal_node = mk_ast_node_mut(ast, literal);
            if (name != MK_AST_STRING_NIL &&
                var_node && type_node && literal_node)
            {
                var_node->as.var_decl.name = name;
                type_node->as.type.kind = MK_TYPE_LIST;
                literal_node->as.literal.kind = MK_LITERAL_INT;
                CC_CHECK(mk_ast_append_child(ast, var, type), "挂接类型");
                CC_CHECK(
                    mk_ast_append_child(ast, var, literal),
                    "挂接字面量"
                );
                CC_CHECK(
                    mk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "list 类型缺少元素类型");
            }
            mk_ast_destroy(ast);
        }

        // 根节点父链接非空
        MKASTNodeId var = MK_AST_NODE_NIL;
        ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建 AST");
        if (ast)
        {
            MKASTNode *root = mk_ast_node_mut(ast, ast->root);
            if (root)
                root->parent = ast->root;
            expect_invalid(ast, "根节点父链接非空");
            mk_ast_destroy(ast);
        }

        // 孤儿节点
        ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建 AST");
        if (ast)
        {
            MKASTNodeId orphan = add_node(ast, MK_AST_TYPE);
            CC_CHECK(orphan != MK_AST_NODE_NIL, "添加孤儿节点");
            expect_invalid(ast, "存在不可达节点");
            mk_ast_destroy(ast);
        }

        // 兄弟链成环
        ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建 AST");
        if (ast)
        {
            const MKASTNode *var_node = mk_ast_node(ast, var);
            if (var_node && var_node->first_child != MK_AST_NODE_NIL)
            {
                MKASTNode *last =
                    mk_ast_node_mut(ast, var_node->last_child);
                if (last)
                    last->next_sibling = var_node->first_child;
            }
            expect_invalid(ast, "子节点链接错误");
            mk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-006 函数声明节点布局
     * ---------------------------------------------------------------- */
    CC_CASE("AST-006", "函数声明节点布局");
    {
        MKAST *ast = mk_ast_create("func.mk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            MKASTStringId func_name = add_text(ast, "build");
            MKASTStringId param_name = add_text(ast, "mode");
            MKASTNodeId func = add_node(ast, MK_AST_FUNC_DECL);
            MKASTNodeId ret = add_node(ast, MK_AST_TYPE);
            MKASTNodeId param = add_node(ast, MK_AST_PARAM);
            MKASTNodeId param_type = add_node(ast, MK_AST_TYPE);
            MKASTNodeId body = add_node(ast, MK_AST_BLOCK);
            MKASTNode *func_node = mk_ast_node_mut(ast, func);
            MKASTNode *ret_node = mk_ast_node_mut(ast, ret);
            MKASTNode *param_node = mk_ast_node_mut(ast, param);
            MKASTNode *param_type_node = mk_ast_node_mut(ast, param_type);
            if (func_name != MK_AST_STRING_NIL &&
                param_name != MK_AST_STRING_NIL &&
                func_node && ret_node && param_node && param_type_node)
            {
                func_node->as.func_decl.name = func_name;
                func_node->as.func_decl.param_count = 1;
                ret_node->as.type.kind = MK_TYPE_INT;
                param_node->as.param.name = param_name;
                param_type_node->as.type.kind = MK_TYPE_STRING;

                CC_CHECK(
                    mk_ast_append_child(ast, param, param_type),
                    "参数类型"
                );
                CC_CHECK(mk_ast_append_child(ast, func, ret), "返回类型");
                CC_CHECK(mk_ast_append_child(ast, func, param), "参数");
                CC_CHECK(mk_ast_append_child(ast, func, body), "函数体");
                CC_CHECK(
                    mk_ast_append_child(ast, ast->root, func),
                    "挂接函数"
                );
                expect_valid(ast);
            }
            mk_ast_destroy(ast);
        }
    }
    CC_CASE_END();



    /* ------------------------------------------------------------------
     * AST-008 类型分类与名称
     * ---------------------------------------------------------------- */
    CC_CASE("AST-008", "类型分类与名称");
    {
        CC_CHECK_EQ_STR("module 名称", "module", mk_ast_kind_name(MK_AST_MODULE));
        CC_CHECK_EQ_STR("var 名称", "var-decl", mk_ast_kind_name(MK_AST_VAR_DECL));
        CC_CHECK_EQ_STR("未知名称", "unknown", mk_ast_kind_name(MK_AST_KIND_COUNT));

        CC_CHECK(
            mk_ast_kind_is_expression(MK_AST_LITERAL),
            "字面量是表达式"
        );
        CC_CHECK(
            !mk_ast_kind_is_expression(MK_AST_ASSIGN),
            "赋值不是表达式"
        );
        CC_CHECK(
            !mk_ast_kind_is_expression(MK_AST_VAR_DECL),
            "变量声明不是表达式"
        );
        CC_CHECK(
            mk_ast_kind_is_statement(MK_AST_ASSIGN),
            "赋值是语句"
        );
        CC_CHECK(
            !mk_ast_kind_is_statement(MK_AST_VAR_DECL),
            "变量声明是块项而不是语句"
        );
        CC_CHECK(
            mk_ast_kind_is_declaration(MK_AST_VAR_DECL),
            "变量声明是声明"
        );
        CC_CHECK(
            !mk_ast_kind_is_declaration(MK_AST_LITERAL),
            "字面量不是声明"
        );
    }
    CC_CASE_END();



    /* ------------------------------------------------------------------
     * AST-010 预算默认值
     * ---------------------------------------------------------------- */
    CC_CASE("AST-010", "预算默认值");
    {
        MKLimits limits;
        memset(&limits, 0xAA, sizeof(limits));
        mk_limits_init(&limits);
        CC_CHECK_EQ_I64("max_tokens", 0, (CCINT64)limits.max_tokens);
        CC_CHECK_EQ_I64("max_ast_nodes", 0, (CCINT64)limits.max_ast_nodes);
        CC_CHECK_EQ_I64(
            "max_syntax_depth", 0, (CCINT64)limits.max_syntax_depth
        );
        CC_CHECK_EQ_I64(
            "max_expression_depth", 0, (CCINT64)limits.max_expression_depth
        );
        CC_CHECK_EQ_I64(
            "max_function_params", 0, (CCINT64)limits.max_function_params
        );
        CC_CHECK_EQ_I64(
            "max_list_elements", 0, (CCINT64)limits.max_list_elements
        );
        CC_CHECK_EQ_I64(
            "max_declarations", 0, (CCINT64)limits.max_declarations
        );
        mk_limits_init(NULL);
    }
    CC_CASE_END();
}
