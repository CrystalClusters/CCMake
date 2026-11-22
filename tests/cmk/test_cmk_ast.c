/*
 * @Description: CMK AST 模型测试：所有权、构建、遍历、校验与预算默认值。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_ast.h"
#include "cmk/cmk_limits.h"

#include <stdio.h>
#include <string.h>

static CMKASTNodeId add_node(CMKAST *ast, CMKASTKind kind)
{
    CMKSourceSpan span;
    memset(&span, 0, sizeof(span));
    return cmk_ast_add_node(ast, kind, &span);
}

static CMKASTStringId add_text(CMKAST *ast, const char *text)
{
    return cmk_ast_add_string(ast, text, (CCUINT32)strlen(text));
}

static CMKAST *build_valid_var_ast(CMKASTNodeId *out_var)
{
    CMKAST *ast = cmk_ast_create("test.cmk");
    if (!ast)
        return NULL;

    CMKASTStringId name = add_text(ast, "count");
    CMKASTNodeId var = add_node(ast, CMK_AST_VAR_DECL);
    CMKASTNodeId type = add_node(ast, CMK_AST_TYPE);
    CMKASTNodeId literal = add_node(ast, CMK_AST_LITERAL);
    if (name == CMK_AST_STRING_NIL ||
        var == CMK_AST_NODE_NIL ||
        type == CMK_AST_NODE_NIL ||
        literal == CMK_AST_NODE_NIL)
        goto Failed;

    CMKASTNode *var_node = cmk_ast_node_mut(ast, var);
    CMKASTNode *type_node = cmk_ast_node_mut(ast, type);
    CMKASTNode *literal_node = cmk_ast_node_mut(ast, literal);
    if (!var_node || !type_node || !literal_node)
        goto Failed;

    var_node->as.var_decl.name = name;
    var_node->as.var_decl.is_const = CCFALSE;
    type_node->as.type.kind = CMK_TYPE_INT;
    literal_node->as.literal.kind = CMK_LITERAL_INT;
    literal_node->as.literal.value.integer = 10;

    if (!cmk_ast_append_child(ast, var, type) ||
        !cmk_ast_append_child(ast, var, literal) ||
        !cmk_ast_append_child(ast, ast->root, var))
        goto Failed;

    if (out_var)
        *out_var = var;
    return ast;

Failed:
    cmk_ast_destroy(ast);
    return NULL;
}

static void expect_valid(CMKAST *ast)
{
    const char *reason = NULL;
    CC_CHECK(cmk_ast_validate(ast, &reason), "AST 校验通过");
    CC_CHECK(reason == NULL, "校验通过时无失败原因");
}

static void expect_invalid(CMKAST *ast, const char *text)
{
    const char *reason = NULL;
    CC_CHECK(!cmk_ast_validate(ast, &reason), "AST 校验应失败");
    CC_CHECK(reason != NULL, "校验失败原因存在");
    if (reason)
        CC_CHECK(strstr(reason, text) != NULL, "校验失败原因匹配");
}

typedef struct ast_visit_log {
    CMKASTKind kinds[64];
    CCUINT32 count;
    CCUINT32 stop_at;
} ASTVisitLog;

static CCBOOL ast_visit_collect(
    const CMKAST *ast, CMKASTNodeId id, void *user
)
{
    ASTVisitLog *log = (ASTVisitLog *)user;
    const CMKASTNode *node = cmk_ast_node(ast, id);
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

void test_cmk_ast(void)
{
    /* ------------------------------------------------------------------
     * AST-001 生命周期与根节点
     * ---------------------------------------------------------------- */
    CC_CASE("AST-001", "生命周期与根节点");
    {
        CMKAST *ast = cmk_ast_create("module.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CC_CHECK_EQ_STR("文件路径", "module.cmk", cmk_ast_file(ast));
            CC_CHECK(
                cmk_ast_root(ast) != CMK_AST_NODE_NIL,
                "根节点存在"
            );
            CC_CHECK_EQ_I64("节点数", 1, (CCINT64)cmk_ast_node_count(ast));
            CC_CHECK_EQ_I64("字符串数", 0, (CCINT64)cmk_ast_string_count(ast));

            const CMKASTNode *root = cmk_ast_node(ast, cmk_ast_root(ast));
            CC_CHECK(root != NULL, "读取根节点");
            if (root)
            {
                CC_CHECK_EQ_I64(
                    "根节点类型", (CCINT64)CMK_AST_MODULE, (CCINT64)root->kind
                );
                CC_CHECK(
                    root->parent == CMK_AST_NODE_NIL,
                    "根节点无父节点"
                );
                CC_CHECK(
                    root->first_child == CMK_AST_NODE_NIL &&
                    root->last_child == CMK_AST_NODE_NIL,
                    "根节点无子节点"
                );
            }

            expect_valid(ast);
            CC_CHECK(
                cmk_ast_node(ast, 999) == NULL,
                "越界节点返回 NULL"
            );
            CC_CHECK(
                cmk_ast_string(ast, 0, NULL) == NULL,
                "越界字符串返回 NULL"
            );

            CMKSourceSpan span;
            memset(&span, 0, sizeof(span));
            CC_CHECK(
                cmk_ast_add_node(ast, CMK_AST_KIND_COUNT, &span) ==
                CMK_AST_NODE_NIL,
                "非法节点类型被拒绝"
            );
            cmk_ast_destroy(ast);
        }

        CMKAST *anonymous = cmk_ast_create(NULL);
        CC_CHECK(anonymous != NULL, "匿名 AST");
        if (anonymous)
        {
            CC_CHECK(cmk_ast_file(anonymous) == NULL, "匿名 AST 无文件路径");
            cmk_ast_destroy(anonymous);
        }

        cmk_ast_destroy(NULL);
        CC_CHECK(cmk_ast_node(NULL, 0) == NULL, "空 AST 查询节点");
        CC_CHECK(cmk_ast_root(NULL) == CMK_AST_NODE_NIL, "空 AST 无根节点");
        CC_CHECK(cmk_ast_node_count(NULL) == 0, "空 AST 节点数为 0");
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-002 文件路径与字符串所有权
     * ---------------------------------------------------------------- */
    CC_CASE("AST-002", "字符串与文件路径所有权");
    {
        char path[] = "a.cmk";
        CMKAST *ast = cmk_ast_create(path);
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            path[0] = 'b';
            CC_CHECK_EQ_STR("文件路径被复制", "a.cmk", cmk_ast_file(ast));

            char data[] = { 'a', '\0', 'b' };
            CMKASTStringId id = cmk_ast_add_string(ast, data, 3);
            CC_CHECK(id != CMK_AST_STRING_NIL, "添加字符串");
            data[0] = 'z';

            CCUINT32 len = 0;
            const char *text = cmk_ast_string(ast, id, &len);
            CC_CHECK(text != NULL, "读取字符串");
            CC_CHECK_EQ_I64("字符串长度", 3, (CCINT64)len);
            CC_CHECK(
                text && text[0] == 'a' && text[1] == '\0' && text[2] == 'b',
                "字符串复制且保留 0 字节"
            );

            CMKASTStringId empty = cmk_ast_add_string(ast, "", 0);
            CC_CHECK(empty != CMK_AST_STRING_NIL, "添加空字符串");
            text = cmk_ast_string(ast, empty, &len);
            CC_CHECK(text != NULL, "读取空字符串");
            CC_CHECK_EQ_I64("空字符串长度", 0, (CCINT64)len);
            CC_CHECK(
                cmk_ast_add_string(ast, NULL, 1) == CMK_AST_STRING_NIL,
                "空指针非零长度被拒绝"
            );

            cmk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-003 构建、链接、遍历与校验
     * ---------------------------------------------------------------- */
    CC_CASE("AST-003", "构建、链接、遍历与校验");
    {
        CMKASTNodeId var = CMK_AST_NODE_NIL;
        CMKAST *ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建变量声明 AST");
        if (ast)
        {
            expect_valid(ast);

            const CMKASTNode *root = cmk_ast_node(ast, cmk_ast_root(ast));
            const CMKASTNode *var_node = cmk_ast_node(ast, var);
            CC_CHECK(root != NULL && var_node != NULL, "读取节点");
            if (root && var_node)
            {
                CC_CHECK(
                    root->first_child == var && root->last_child == var,
                    "根节点子链接"
                );
                CC_CHECK(
                    var_node->parent == cmk_ast_root(ast),
                    "变量父链接"
                );
                CC_CHECK(
                    var_node->first_child != CMK_AST_NODE_NIL &&
                    var_node->last_child != CMK_AST_NODE_NIL,
                    "变量子链接"
                );
            }

            ASTVisitLog log;
            memset(&log, 0, sizeof(log));
            CC_CHECK(
                cmk_ast_visit(
                    ast, cmk_ast_root(ast), ast_visit_collect, &log
                ),
                "遍历 AST"
            );
            CC_CHECK_EQ_I64("访问节点数", 4, (CCINT64)log.count);
            if (log.count == 4)
            {
                CC_CHECK_EQ_I64(
                    "访问 0", (CCINT64)CMK_AST_MODULE,
                    (CCINT64)log.kinds[0]
                );
                CC_CHECK_EQ_I64(
                    "访问 1", (CCINT64)CMK_AST_VAR_DECL,
                    (CCINT64)log.kinds[1]
                );
                CC_CHECK_EQ_I64(
                    "访问 2", (CCINT64)CMK_AST_TYPE,
                    (CCINT64)log.kinds[2]
                );
                CC_CHECK_EQ_I64(
                    "访问 3", (CCINT64)CMK_AST_LITERAL,
                    (CCINT64)log.kinds[3]
                );
            }

            log.count = 0;
            log.stop_at = 1;
            CC_CHECK(
                !cmk_ast_visit(
                    ast, cmk_ast_root(ast), ast_visit_collect, &log
                ),
                "visitor 中断"
            );
            CC_CHECK_EQ_I64("中断时只访问一个节点", 1, (CCINT64)log.count);

            cmk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-004 子节点挂接规则
     * ---------------------------------------------------------------- */
    CC_CASE("AST-004", "子节点挂接规则");
    {
        CMKAST *ast = cmk_ast_create("links.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CMKASTNodeId a = add_node(ast, CMK_AST_VAR_DECL);
            CMKASTNodeId b = add_node(ast, CMK_AST_VAR_DECL);
            if (a != CMK_AST_NODE_NIL && b != CMK_AST_NODE_NIL)
            {
                CC_CHECK(cmk_ast_append_child(ast, a, b), "首次挂接");
                CC_CHECK(!cmk_ast_append_child(ast, a, b), "拒绝重复挂接");
                CC_CHECK(!cmk_ast_append_child(ast, a, a), "拒绝自挂接");
                CC_CHECK(
                    !cmk_ast_append_child(ast, a, ast->root),
                    "拒绝挂接根节点"
                );
                CC_CHECK(
                    !cmk_ast_append_child(ast, a, 999),
                    "拒绝越界子节点"
                );
                CC_CHECK(
                    !cmk_ast_append_child(ast, b, a),
                    "拒绝形成祖先环"
                );
            }
            cmk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-005 校验失败路径
     * ---------------------------------------------------------------- */
    CC_CASE("AST-005", "校验失败路径");
    {
        // 子节点数量错误
        CMKAST *ast = cmk_ast_create("bad-count.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CMKASTStringId name = add_text(ast, "x");
            CMKASTNodeId var = add_node(ast, CMK_AST_VAR_DECL);
            CMKASTNodeId type = add_node(ast, CMK_AST_TYPE);
            CMKASTNode *var_node = cmk_ast_node_mut(ast, var);
            CMKASTNode *type_node = cmk_ast_node_mut(ast, type);
            if (name != CMK_AST_STRING_NIL && var_node && type_node)
            {
                var_node->as.var_decl.name = name;
                type_node->as.type.kind = CMK_TYPE_INT;
                CC_CHECK(cmk_ast_append_child(ast, var, type), "挂接类型");
                CC_CHECK(
                    cmk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "变量声明子节点数量错误");
            }
            cmk_ast_destroy(ast);
        }

        // 子节点类型错误
        ast = cmk_ast_create("bad-kind.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CMKASTStringId name = add_text(ast, "x");
            CMKASTNodeId var = add_node(ast, CMK_AST_VAR_DECL);
            CMKASTNodeId lit1 = add_node(ast, CMK_AST_LITERAL);
            CMKASTNodeId lit2 = add_node(ast, CMK_AST_LITERAL);
            CMKASTNode *var_node = cmk_ast_node_mut(ast, var);
            if (name != CMK_AST_STRING_NIL && var_node)
            {
                var_node->as.var_decl.name = name;
                CC_CHECK(cmk_ast_append_child(ast, var, lit1), "挂接 lit1");
                CC_CHECK(cmk_ast_append_child(ast, var, lit2), "挂接 lit2");
                CC_CHECK(
                    cmk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "变量声明子节点类型错误");
            }
            cmk_ast_destroy(ast);
        }

        // 字符串引用无效
        ast = cmk_ast_create("bad-string.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CMKASTNodeId var = add_node(ast, CMK_AST_VAR_DECL);
            CMKASTNodeId type = add_node(ast, CMK_AST_TYPE);
            CMKASTNodeId literal = add_node(ast, CMK_AST_LITERAL);
            CMKASTNode *var_node = cmk_ast_node_mut(ast, var);
            CMKASTNode *type_node = cmk_ast_node_mut(ast, type);
            CMKASTNode *literal_node = cmk_ast_node_mut(ast, literal);
            if (var_node && type_node && literal_node)
            {
                var_node->as.var_decl.name = CMK_AST_STRING_NIL;
                type_node->as.type.kind = CMK_TYPE_INT;
                literal_node->as.literal.kind = CMK_LITERAL_INT;
                CC_CHECK(cmk_ast_append_child(ast, var, type), "挂接类型");
                CC_CHECK(
                    cmk_ast_append_child(ast, var, literal),
                    "挂接字面量"
                );
                CC_CHECK(
                    cmk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "变量名无效");
            }
            cmk_ast_destroy(ast);
        }

        // list 类型缺少元素类型
        ast = cmk_ast_create("bad-list.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CMKASTStringId name = add_text(ast, "x");
            CMKASTNodeId var = add_node(ast, CMK_AST_VAR_DECL);
            CMKASTNodeId type = add_node(ast, CMK_AST_TYPE);
            CMKASTNodeId literal = add_node(ast, CMK_AST_LITERAL);
            CMKASTNode *var_node = cmk_ast_node_mut(ast, var);
            CMKASTNode *type_node = cmk_ast_node_mut(ast, type);
            CMKASTNode *literal_node = cmk_ast_node_mut(ast, literal);
            if (name != CMK_AST_STRING_NIL &&
                var_node && type_node && literal_node)
            {
                var_node->as.var_decl.name = name;
                type_node->as.type.kind = CMK_TYPE_LIST;
                literal_node->as.literal.kind = CMK_LITERAL_INT;
                CC_CHECK(cmk_ast_append_child(ast, var, type), "挂接类型");
                CC_CHECK(
                    cmk_ast_append_child(ast, var, literal),
                    "挂接字面量"
                );
                CC_CHECK(
                    cmk_ast_append_child(ast, ast->root, var),
                    "挂接变量"
                );
                expect_invalid(ast, "list 类型缺少元素类型");
            }
            cmk_ast_destroy(ast);
        }

        // 根节点父链接非空
        CMKASTNodeId var = CMK_AST_NODE_NIL;
        ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建 AST");
        if (ast)
        {
            CMKASTNode *root = cmk_ast_node_mut(ast, ast->root);
            if (root)
                root->parent = ast->root;
            expect_invalid(ast, "根节点父链接非空");
            cmk_ast_destroy(ast);
        }

        // 孤儿节点
        ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建 AST");
        if (ast)
        {
            CMKASTNodeId orphan = add_node(ast, CMK_AST_TYPE);
            CC_CHECK(orphan != CMK_AST_NODE_NIL, "添加孤儿节点");
            expect_invalid(ast, "存在不可达节点");
            cmk_ast_destroy(ast);
        }

        // 兄弟链成环
        ast = build_valid_var_ast(&var);
        CC_CHECK(ast != NULL, "构建 AST");
        if (ast)
        {
            const CMKASTNode *var_node = cmk_ast_node(ast, var);
            if (var_node && var_node->first_child != CMK_AST_NODE_NIL)
            {
                CMKASTNode *last =
                    cmk_ast_node_mut(ast, var_node->last_child);
                if (last)
                    last->next_sibling = var_node->first_child;
            }
            expect_invalid(ast, "子节点链接错误");
            cmk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-006 函数声明节点布局
     * ---------------------------------------------------------------- */
    CC_CASE("AST-006", "函数声明节点布局");
    {
        CMKAST *ast = cmk_ast_create("func.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CMKASTStringId func_name = add_text(ast, "build");
            CMKASTStringId param_name = add_text(ast, "mode");
            CMKASTNodeId func = add_node(ast, CMK_AST_FUNC_DECL);
            CMKASTNodeId ret = add_node(ast, CMK_AST_TYPE);
            CMKASTNodeId param = add_node(ast, CMK_AST_PARAM);
            CMKASTNodeId param_type = add_node(ast, CMK_AST_TYPE);
            CMKASTNodeId body = add_node(ast, CMK_AST_BLOCK);
            CMKASTNode *func_node = cmk_ast_node_mut(ast, func);
            CMKASTNode *ret_node = cmk_ast_node_mut(ast, ret);
            CMKASTNode *param_node = cmk_ast_node_mut(ast, param);
            CMKASTNode *param_type_node = cmk_ast_node_mut(ast, param_type);
            if (func_name != CMK_AST_STRING_NIL &&
                param_name != CMK_AST_STRING_NIL &&
                func_node && ret_node && param_node && param_type_node)
            {
                func_node->as.func_decl.name = func_name;
                func_node->as.func_decl.param_count = 1;
                ret_node->as.type.kind = CMK_TYPE_INT;
                param_node->as.param.name = param_name;
                param_type_node->as.type.kind = CMK_TYPE_STRING;

                CC_CHECK(
                    cmk_ast_append_child(ast, param, param_type),
                    "参数类型"
                );
                CC_CHECK(cmk_ast_append_child(ast, func, ret), "返回类型");
                CC_CHECK(cmk_ast_append_child(ast, func, param), "参数");
                CC_CHECK(cmk_ast_append_child(ast, func, body), "函数体");
                CC_CHECK(
                    cmk_ast_append_child(ast, ast->root, func),
                    "挂接函数"
                );
                expect_valid(ast);
            }
            cmk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-007 target 与导入节点布局
     * ---------------------------------------------------------------- */
    CC_CASE("AST-007", "target 与导入节点布局");
    {
        CMKAST *ast = cmk_ast_create("target.cmk");
        CC_CHECK(ast != NULL, "创建 AST");
        if (ast)
        {
            CMKASTStringId target_name = add_text(ast, "app");
            CMKASTStringId field_name = add_text(ast, "outputs");
            CMKASTStringId module_path = add_text(ast, "targets/common.cmk");
            CMKASTStringId alias = add_text(ast, "common");
            CMKASTStringId core = add_text(ast, "core");
            CMKASTStringId utils = add_text(ast, "utils");
            CMKASTStringId output = add_text(ast, "build/app");

            CMKASTNodeId target = add_node(ast, CMK_AST_TARGET_DECL);
            CMKASTNodeId field = add_node(ast, CMK_AST_TARGET_FIELD);
            CMKASTNodeId field_type = add_node(ast, CMK_AST_TYPE);
            CMKASTNodeId string_type = add_node(ast, CMK_AST_TYPE);
            CMKASTNodeId list = add_node(ast, CMK_AST_LIST);
            CMKASTNodeId literal = add_node(ast, CMK_AST_LITERAL);
            CMKASTNodeId import = add_node(ast, CMK_AST_IMPORT_DECL);
            CMKASTNodeId item_core = add_node(ast, CMK_AST_IMPORT_ITEM);
            CMKASTNodeId item_utils = add_node(ast, CMK_AST_IMPORT_ITEM);

            CMKASTNode *target_node = cmk_ast_node_mut(ast, target);
            CMKASTNode *field_node = cmk_ast_node_mut(ast, field);
            CMKASTNode *field_type_node = cmk_ast_node_mut(ast, field_type);
            CMKASTNode *string_type_node = cmk_ast_node_mut(ast, string_type);
            CMKASTNode *literal_node = cmk_ast_node_mut(ast, literal);
            CMKASTNode *import_node = cmk_ast_node_mut(ast, import);
            CMKASTNode *core_node = cmk_ast_node_mut(ast, item_core);
            CMKASTNode *utils_node = cmk_ast_node_mut(ast, item_utils);

            if (target_name != CMK_AST_STRING_NIL &&
                field_name != CMK_AST_STRING_NIL &&
                module_path != CMK_AST_STRING_NIL &&
                alias != CMK_AST_STRING_NIL &&
                core != CMK_AST_STRING_NIL &&
                utils != CMK_AST_STRING_NIL &&
                output != CMK_AST_STRING_NIL &&
                target_node && field_node && field_type_node &&
                string_type_node && literal_node && import_node &&
                core_node && utils_node)
            {
                target_node->as.target_decl.name = target_name;
                field_node->as.target_field.field = CMK_TARGET_FIELD_OUTPUTS;
                field_node->as.target_field.name = field_name;
                field_type_node->as.type.kind = CMK_TYPE_LIST;
                string_type_node->as.type.kind = CMK_TYPE_STRING;
                literal_node->as.literal.kind = CMK_LITERAL_STRING;
                literal_node->as.literal.value.string = output;
                import_node->as.import_decl.kind = CMK_IMPORT_TARGET;
                import_node->as.import_decl.module = module_path;
                import_node->as.import_decl.alias = alias;
                import_node->as.import_decl.wildcard = CCFALSE;
                core_node->as.import_item.name = core;
                core_node->as.import_item.alias = CMK_AST_STRING_NIL;
                utils_node->as.import_item.name = utils;
                utils_node->as.import_item.alias = CMK_AST_STRING_NIL;

                CC_CHECK(
                    cmk_ast_append_child(ast, field_type, string_type),
                    "list 元素类型"
                );
                CC_CHECK(
                    cmk_ast_append_child(ast, list, literal),
                    "列表元素"
                );
                CC_CHECK(cmk_ast_append_child(ast, field, field_type), "字段类型");
                CC_CHECK(cmk_ast_append_child(ast, field, list), "字段值");
                CC_CHECK(cmk_ast_append_child(ast, target, field), "target 字段");
                CC_CHECK(cmk_ast_append_child(ast, import, item_core), "导入 core");
                CC_CHECK(
                    cmk_ast_append_child(ast, import, item_utils),
                    "导入 utils"
                );
                CC_CHECK(cmk_ast_append_child(ast, ast->root, import), "挂接导入");
                CC_CHECK(cmk_ast_append_child(ast, ast->root, target), "挂接 target");
                expect_valid(ast);
            }
            cmk_ast_destroy(ast);
        }
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-008 类型分类与名称
     * ---------------------------------------------------------------- */
    CC_CASE("AST-008", "类型分类与名称");
    {
        CC_CHECK_EQ_STR("module 名称", "module", cmk_ast_kind_name(CMK_AST_MODULE));
        CC_CHECK_EQ_STR("var 名称", "var-decl", cmk_ast_kind_name(CMK_AST_VAR_DECL));
        CC_CHECK_EQ_STR("未知名称", "unknown", cmk_ast_kind_name(CMK_AST_KIND_COUNT));

        CC_CHECK(
            cmk_ast_kind_is_expression(CMK_AST_LITERAL),
            "字面量是表达式"
        );
        CC_CHECK(
            !cmk_ast_kind_is_expression(CMK_AST_ASSIGN),
            "赋值不是表达式"
        );
        CC_CHECK(
            !cmk_ast_kind_is_expression(CMK_AST_VAR_DECL),
            "变量声明不是表达式"
        );
        CC_CHECK(
            cmk_ast_kind_is_statement(CMK_AST_ASSIGN),
            "赋值是语句"
        );
        CC_CHECK(
            !cmk_ast_kind_is_statement(CMK_AST_VAR_DECL),
            "变量声明是块项而不是语句"
        );
        CC_CHECK(
            cmk_ast_kind_is_declaration(CMK_AST_VAR_DECL),
            "变量声明是声明"
        );
        CC_CHECK(
            !cmk_ast_kind_is_declaration(CMK_AST_LITERAL),
            "字面量不是声明"
        );
    }
    CC_CASE_END();

    /* ------------------------------------------------------------------
     * AST-009 预算默认值
     * ---------------------------------------------------------------- */
    CC_CASE("AST-009", "预算默认值");
    {
        CMKLimits limits;
        memset(&limits, 0xAA, sizeof(limits));
        cmk_limits_init(&limits);
        CC_CHECK_EQ_I64("max_tokens", 0, (CCINT64)limits.max_tokens);
        CC_CHECK_EQ_I64("max_ast_nodes", 0, (CCINT64)limits.max_ast_nodes);
        CC_CHECK_EQ_I64(
            "max_syntax_depth", 1024, (CCINT64)limits.max_syntax_depth
        );
        CC_CHECK_EQ_I64(
            "max_expression_depth", 512, (CCINT64)limits.max_expression_depth
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
        cmk_limits_init(NULL);
    }
    CC_CASE_END();
}
