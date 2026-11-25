/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK AST 节点、字符串表、源码范围与遍历接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_ast.h"
#include "mk_safe.h"

#include <ccalloc.h>

#include <string.h>

#define MK_AST_COMMENT CC_DBG_REL_TAG(".mk AST", NULL)

#define MK_AST_NODE_ALIGN   ((CCUINT32)sizeof(MKASTNode))
#define MK_AST_NODE_SIZE    ((CCINT32)sizeof(MKASTNode))
#define MK_AST_STRING_ALIGN ((CCUINT32)sizeof(MKASTString))
#define MK_AST_STRING_SIZE  ((CCINT32)sizeof(MKASTString))
#define MK_AST_ID_ALIGN     ((CCUINT32)sizeof(MKASTNodeId))
#define MK_AST_ID_SIZE      ((CCINT32)sizeof(MKASTNodeId))

/* ============================================================================
 * 内部数组访问
 * ========================================================================== */

static CCUINT32 ast_node_count_internal(const MKAST *ast)
{
    if (!ast)
        return 0;
    return ast->nodes.size / (CCUINT32)sizeof(MKASTNode);
}

static CCUINT32 ast_string_count_internal(const MKAST *ast)
{
    if (!ast)
        return 0;
    return ast->strings.size / (CCUINT32)sizeof(MKASTString);
}

static const MKASTNode *ast_node_at_const(const MKAST *ast, MKASTNodeId id)
{
    if (!ast || id >= ast_node_count_internal(ast))
        return NULL;
    return &((const MKASTNode *)ast->nodes.array)[id];
}

static MKASTNode *ast_node_at_mut(MKAST *ast, MKASTNodeId id)
{
    if (!ast || id >= ast_node_count_internal(ast))
        return NULL;
    return &((MKASTNode *)ast->nodes.array)[id];
}

static const MKASTString *ast_string_at(
    const MKAST *ast, MKASTStringId id
)
{
    if (!ast || id >= ast_string_count_internal(ast))
        return NULL;
    return &((const MKASTString *)ast->strings.array)[id];
}

static char *ast_copy_cstr(const char *text)
{
    size_t len = strlen(text);
    char *copy = ccalloc(NULL, (CCUINT64)len + 1, MK_AST_COMMENT);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

/* ============================================================================
 * 基础分类与名称
 * ========================================================================== */

const char *mk_ast_kind_name(MKASTKind kind)
{
    switch (kind)
    {
    case MK_AST_MODULE:
        return "module";

    case MK_AST_INCLUDE_DECL:
        return "include-decl";

    case MK_AST_IMPORT_DECL:
        return "import-decl";

    case MK_AST_IMPORT_ITEM:
        return "import-item";

    case MK_AST_VAR_DECL:
        return "var-decl";

    case MK_AST_FUNC_DECL:
        return "func-decl";

    case MK_AST_PARAM:
        return "param";

    case MK_AST_TYPE:
        return "type";

    case MK_AST_BLOCK:
        return "block";

    case MK_AST_EXPR_STMT:
        return "expr-stmt";

    case MK_AST_IF:
        return "if";

    case MK_AST_WHILE:
        return "while";

    case MK_AST_FOR_IN:
        return "for-in";

    case MK_AST_RETURN:
        return "return";

    case MK_AST_BREAK:
        return "break";

    case MK_AST_CONTINUE:
        return "continue";

    case MK_AST_ASSIGN:
        return "assign";

    case MK_AST_LITERAL:
        return "literal";

    case MK_AST_IDENT:
        return "ident";

    case MK_AST_DISCARD:
        return "discard";

    case MK_AST_UNARY:
        return "unary";

    case MK_AST_BINARY:
        return "binary";

    case MK_AST_CALL:
        return "call";

    case MK_AST_INDEX:
        return "index";

    case MK_AST_MEMBER:
        return "member";

    case MK_AST_LIST:
        return "list";

    case MK_AST_CONFIG_REF:
        return "config-ref";

    default:
        return "unknown";
    }
}

CCBOOL mk_ast_kind_is_expression(MKASTKind kind)
{
    switch (kind)
    {
    case MK_AST_LITERAL:
    case MK_AST_IDENT:
    case MK_AST_UNARY:
    case MK_AST_BINARY:
    case MK_AST_CALL:
    case MK_AST_INDEX:
    case MK_AST_MEMBER:
    case MK_AST_LIST:
    case MK_AST_CONFIG_REF:
        return CCTRUE;

    default:
        return CCFALSE;
    }
}

CCBOOL mk_ast_kind_is_statement(MKASTKind kind)
{
    switch (kind)
    {
    case MK_AST_BLOCK:
    case MK_AST_EXPR_STMT:
    case MK_AST_IF:
    case MK_AST_WHILE:
    case MK_AST_FOR_IN:
    case MK_AST_RETURN:
    case MK_AST_BREAK:
    case MK_AST_CONTINUE:
    case MK_AST_ASSIGN:
        return CCTRUE;

    default:
        return CCFALSE;
    }
}

CCBOOL mk_ast_kind_is_declaration(MKASTKind kind)
{
    switch (kind)
    {
    case MK_AST_INCLUDE_DECL:
    case MK_AST_IMPORT_DECL:
    case MK_AST_VAR_DECL:
    case MK_AST_FUNC_DECL:
        return CCTRUE;

    default:
        return CCFALSE;
    }
}

static CCBOOL ast_is_top_item_kind(MKASTKind kind)
{
    switch (kind)
    {
    case MK_AST_INCLUDE_DECL:
    case MK_AST_IMPORT_DECL:
    case MK_AST_VAR_DECL:
    case MK_AST_FUNC_DECL:
        return CCTRUE;

    default:
        return CCFALSE;
    }
}

static CCBOOL ast_is_normal_statement(
    const MKAST *ast, MKASTNodeId id
)
{
    const MKASTNode *node = ast_node_at_const(ast, id);
    if (!node || !mk_ast_kind_is_statement(node->kind))
        return CCFALSE;
    if (node->kind == MK_AST_IF)
        return node->as.if_stmt.is_top_level == CCFALSE
            ? CCTRUE : CCFALSE;
    if (node->kind == MK_AST_BLOCK)
        return node->as.block.is_top_level == CCFALSE
            ? CCTRUE : CCFALSE;
    return CCTRUE;
}

static CCBOOL ast_is_top_branch(
    const MKAST *ast, MKASTNodeId id
)
{
    const MKASTNode *node = ast_node_at_const(ast, id);
    if (!node)
        return CCFALSE;
    if (node->kind == MK_AST_IF)
        return node->as.if_stmt.is_top_level;
    if (node->kind == MK_AST_BLOCK)
        return node->as.block.is_top_level;
    return CCFALSE;
}

/* ============================================================================
 * 生命周期
 * ========================================================================== */

MKAST *mk_ast_create(const char *file)
{
    MKAST *ast = ccalloc(NULL, sizeof(MKAST), MK_AST_COMMENT);
    if (!ast)
        return NULL;
    memset(ast, 0, sizeof(*ast));

    ast->root = MK_AST_NODE_NIL;
    if (file)
    {
        ast->file = ast_copy_cstr(file);
        if (!ast->file)
        {
            mk_ast_destroy(ast);
            return NULL;
        }
    }

    MKSourceSpan span;
    memset(&span, 0, sizeof(span));
    MKASTNodeId root = mk_ast_add_node(ast, MK_AST_MODULE, &span);
    if (root == MK_AST_NODE_NIL)
    {
        mk_ast_destroy(ast);
        return NULL;
    }

    ast->root = root;
    return ast;
}

void mk_ast_destroy(MKAST *ast)
{
    if (!ast)
        return;

    CCUINT32 string_count = ast_string_count_internal(ast);
    const MKASTString *strings = (const MKASTString *)ast->strings.array;
    for (CCUINT32 i = 0; i < string_count; i++)
    {
        if (strings[i].data)
            ccalloc(strings[i].data, 0, NULL);
    }
    if (ast->strings.array)
        ccalloc(ast->strings.array, 0, NULL);
    if (ast->nodes.array)
        ccalloc(ast->nodes.array, 0, NULL);
    if (ast->file)
        ccalloc(ast->file, 0, NULL);

    ccalloc(ast, 0, NULL);
}

MKASTNodeId mk_ast_add_node(
    MKAST *ast, MKASTKind kind, const MKSourceSpan *span
)
{
    if (!ast || (unsigned int)kind >= (unsigned int)MK_AST_KIND_COUNT)
        return MK_AST_NODE_NIL;

    CCUINT32 index = ast_node_count_internal(ast);
    if (index == MK_AST_NODE_NIL)
        return MK_AST_NODE_NIL;

    MKASTNode node;
    memset(&node, 0, sizeof(node));
    node.kind = kind;
    node.parent = MK_AST_NODE_NIL;
    node.first_child = MK_AST_NODE_NIL;
    node.last_child = MK_AST_NODE_NIL;
    node.next_sibling = MK_AST_NODE_NIL;
    if (span)
        node.span = *span;

    if (!mk_dyn_push(
            &ast->nodes, MK_AST_NODE_ALIGN, MK_AST_NODE_SIZE, &node
        ))
        return MK_AST_NODE_NIL;
    return index;
}

MKASTStringId mk_ast_add_string(
    MKAST *ast, const char *data, CCUINT32 len
)
{
    if (!ast || (!data && len > 0))
        return MK_AST_STRING_NIL;

    CCUINT32 index = ast_string_count_internal(ast);
    if (index == MK_AST_STRING_NIL)
        return MK_AST_STRING_NIL;

    char *copy = ccalloc(NULL, (CCUINT64)len + 1, MK_AST_COMMENT);
    if (!copy)
        return MK_AST_STRING_NIL;
    if (len > 0)
        memcpy(copy, data, len);
    copy[len] = '\0';

    MKASTString string;
    memset(&string, 0, sizeof(string));
    string.data = copy;
    string.len = len;

    if (!mk_dyn_push(
            &ast->strings, MK_AST_STRING_ALIGN, MK_AST_STRING_SIZE,
            &string
        ))
    {
        ccalloc(copy, 0, NULL);
        return MK_AST_STRING_NIL;
    }
    return index;
}

CCBOOL mk_ast_append_child(
    MKAST *ast, MKASTNodeId parent, MKASTNodeId child
)
{
    if (!ast)
        return CCFALSE;

    CCUINT32 count = ast_node_count_internal(ast);
    if (parent >= count || child >= count)
        return CCFALSE;
    if (parent == child || child == ast->root)
        return CCFALSE;

    MKASTNode *parent_node = ast_node_at_mut(ast, parent);
    MKASTNode *child_node = ast_node_at_mut(ast, child);
    if (!parent_node || !child_node)
        return CCFALSE;
    if (child_node->parent != MK_AST_NODE_NIL)
        return CCFALSE;

    if ((parent_node->first_child == MK_AST_NODE_NIL) !=
        (parent_node->last_child == MK_AST_NODE_NIL))
        return CCFALSE;

    // 拒绝把祖先挂到后代下面形成环
    MKASTNodeId cursor = parent;
    CCUINT32 guard = 0;
    while (cursor != MK_AST_NODE_NIL)
    {
        if (cursor == child)
            return CCFALSE;
        if (cursor >= count)
            return CCFALSE;
        const MKASTNode *cursor_node = ast_node_at_const(ast, cursor);
        if (!cursor_node)
            return CCFALSE;
        cursor = cursor_node->parent;
        if (++guard > count)
            return CCFALSE;
    }

    if (parent_node->first_child == MK_AST_NODE_NIL)
        parent_node->first_child = child;
    else
    {
        MKASTNode *last = ast_node_at_mut(ast, parent_node->last_child);
        if (!last)
            return CCFALSE;
        last->next_sibling = child;
    }

    child_node->parent = parent;
    child_node->next_sibling = MK_AST_NODE_NIL;
    parent_node->last_child = child;
    return CCTRUE;
}

/* ============================================================================
 * 查询
 * ========================================================================== */

MKASTNode *mk_ast_node_mut(MKAST *ast, MKASTNodeId id)
{
    return ast_node_at_mut(ast, id);
}

const MKASTNode *mk_ast_node(const MKAST *ast, MKASTNodeId id)
{
    return ast_node_at_const(ast, id);
}

const char *mk_ast_string(
    const MKAST *ast, MKASTStringId id, CCUINT32 *out_len
)
{
    if (out_len)
        *out_len = 0;

    const MKASTString *string = ast_string_at(ast, id);
    if (!string)
        return NULL;
    if (out_len)
        *out_len = string->len;
    return string->data;
}

const char *mk_ast_file(const MKAST *ast)
{
    return ast ? ast->file : NULL;
}

MKASTNodeId mk_ast_root(const MKAST *ast)
{
    return ast ? ast->root : MK_AST_NODE_NIL;
}

CCUINT32 mk_ast_node_count(const MKAST *ast)
{
    return ast_node_count_internal(ast);
}

CCUINT32 mk_ast_string_count(const MKAST *ast)
{
    return ast_string_count_internal(ast);
}

/* ============================================================================
 * 校验
 * ========================================================================== */

static CCBOOL ast_fail(const char **reason, const char *text)
{
    if (reason)
        *reason = text;
    return CCFALSE;
}

static CCBOOL ast_string_ok(const MKAST *ast, MKASTStringId id)
{
    return (id != MK_AST_STRING_NIL &&
            id < ast_string_count_internal(ast)) ? CCTRUE : CCFALSE;
}

static CCBOOL ast_check_children(
    const MKAST *ast,
    MKASTNodeId id,
    const MKASTNode *node,
    CCUINT32 *out_count
)
{
    if ((node->first_child == MK_AST_NODE_NIL) !=
        (node->last_child == MK_AST_NODE_NIL))
        return CCFALSE;

    CCUINT32 count = ast_node_count_internal(ast);
    CCUINT32 child_count = 0;
    MKASTNodeId child = node->first_child;
    MKASTNodeId last = MK_AST_NODE_NIL;

    while (child != MK_AST_NODE_NIL)
    {
        const MKASTNode *child_node = ast_node_at_const(ast, child);
        if (!child_node)
            return CCFALSE;
        if (child_node->parent != id)
            return CCFALSE;
        if (++child_count > count)
            return CCFALSE;
        last = child;
        child = child_node->next_sibling;
    }

    if (child_count > 0 && last != node->last_child)
        return CCFALSE;
    if (out_count)
        *out_count = child_count;
    return CCTRUE;
}

static MKASTNodeId ast_nth_child(
    const MKAST *ast, MKASTNodeId id, CCUINT32 index
)
{
    const MKASTNode *node = ast_node_at_const(ast, id);
    if (!node)
        return MK_AST_NODE_NIL;

    MKASTNodeId child = node->first_child;
    CCUINT32 guard = 0;
    CCUINT32 count = ast_node_count_internal(ast);
    while (child != MK_AST_NODE_NIL && guard < index)
    {
        const MKASTNode *child_node = ast_node_at_const(ast, child);
        if (!child_node || ++guard > count)
            return MK_AST_NODE_NIL;
        child = child_node->next_sibling;
    }
    return child;
}

static CCBOOL ast_child_is_kind(
    const MKAST *ast, MKASTNodeId id, CCUINT32 index, MKASTKind kind
)
{
    MKASTNodeId child = ast_nth_child(ast, id, index);
    const MKASTNode *node = ast_node_at_const(ast, child);
    return (node && node->kind == kind) ? CCTRUE : CCFALSE;
}

static CCBOOL ast_child_is_expression(
    const MKAST *ast, MKASTNodeId id, CCUINT32 index
)
{
    MKASTNodeId child = ast_nth_child(ast, id, index);
    const MKASTNode *node = ast_node_at_const(ast, child);
    return (node && mk_ast_kind_is_expression(node->kind))
        ? CCTRUE : CCFALSE;
}

static CCBOOL ast_child_is_normal_branch(
    const MKAST *ast, MKASTNodeId id, CCUINT32 index
)
{
    MKASTNodeId child = ast_nth_child(ast, id, index);
    return ast_is_normal_statement(ast, child);
}

static CCBOOL ast_child_is_top_branch(
    const MKAST *ast, MKASTNodeId id, CCUINT32 index
)
{
    MKASTNodeId child = ast_nth_child(ast, id, index);
    return ast_is_top_branch(ast, child);
}

static CCBOOL ast_children_all_expression(
    const MKAST *ast, MKASTNodeId id, CCUINT32 child_count
)
{
    for (CCUINT32 i = 0; i < child_count; i++)
    {
        if (!ast_child_is_expression(ast, id, i))
            return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL ast_children_all_kind(
    const MKAST *ast, MKASTNodeId id,
    CCUINT32 child_count, MKASTKind kind
)
{
    for (CCUINT32 i = 0; i < child_count; i++)
    {
        if (!ast_child_is_kind(ast, id, i, kind))
            return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL ast_validate_decl(
    const MKAST *ast, MKASTNodeId id, const MKASTNode *node,
    CCUINT32 child_count, const char **reason
)
{
    switch (node->kind)
    {
    case MK_AST_MODULE:
    {
        MKASTNodeId child = node->first_child;
        while (child != MK_AST_NODE_NIL)
        {
            const MKASTNode *child_node = ast_node_at_const(ast, child);
            if (!child_node || !ast_is_top_item_kind(child_node->kind))
                return ast_fail(reason, "module 子节点类型非法");
            if (child_node->kind == MK_AST_IF &&
                !child_node->as.if_stmt.is_top_level)
                return ast_fail(reason, "module 中的 if 必须是顶层条件");
            child = child_node->next_sibling;
        }
        break;
    }

    case MK_AST_INCLUDE_DECL:
        if (child_count != 0)
            return ast_fail(reason, "include 子节点数量错误");
        if (!ast_string_ok(ast, node->as.include_decl.module))
            return ast_fail(reason, "include 路径无效");
        break;

    case MK_AST_IMPORT_DECL:
    {
        if ((unsigned int)node->as.import_decl.kind >=
            (unsigned int)MK_IMPORT_KIND_COUNT)
            return ast_fail(reason, "导入类型非法");
        if (!ast_string_ok(ast, node->as.import_decl.module))
            return ast_fail(reason, "导入路径无效");
        if (node->as.import_decl.kind == MK_IMPORT_FUNC)
        {
            if (node->as.import_decl.alias != MK_AST_STRING_NIL)
                return ast_fail(reason, "函数导入不应有模块别名");
            if (node->as.import_decl.wildcard)
            {
                if (child_count != 0)
                    return ast_fail(reason, "通配导入不应有导入项");
            }
            else if (child_count == 0 ||
                     !ast_children_all_kind(
                         ast, id, child_count, MK_AST_IMPORT_ITEM
                     ))
                return ast_fail(reason, "函数导入项非法");
        }
        else
        {
            if (node->as.import_decl.wildcard)
                return ast_fail(reason, "目标导入不能通配");
            if (!ast_string_ok(ast, node->as.import_decl.alias))
                return ast_fail(reason, "目标导入缺少别名");
            if (child_count == 0 ||
                !ast_children_all_kind(
                    ast, id, child_count, MK_AST_IMPORT_ITEM
                ))
                return ast_fail(reason, "目标导入项非法");
        }
        break;
    }

    case MK_AST_IMPORT_ITEM:
        if (child_count != 0)
            return ast_fail(reason, "导入项子节点数量错误");
        if (!ast_string_ok(ast, node->as.import_item.name))
            return ast_fail(reason, "导入名无效");
        if (node->as.import_item.alias != MK_AST_STRING_NIL &&
            !ast_string_ok(ast, node->as.import_item.alias))
            return ast_fail(reason, "导入别名无效");
        break;

    case MK_AST_VAR_DECL:
        if (child_count != 2)
            return ast_fail(reason, "变量声明子节点数量错误");
        if (!ast_child_is_kind(ast, id, 0, MK_AST_TYPE) ||
            !ast_child_is_expression(ast, id, 1))
            return ast_fail(reason, "变量声明子节点类型错误");
        if (!ast_string_ok(ast, node->as.var_decl.name))
            return ast_fail(reason, "变量名无效");
        break;

    case MK_AST_FUNC_DECL:
        if (child_count < 2)
            return ast_fail(reason, "函数声明子节点数量错误");
        if (node->as.func_decl.param_count != child_count - 2)
            return ast_fail(reason, "函数参数数量与子节点不一致");
        if (!ast_child_is_kind(ast, id, 0, MK_AST_TYPE))
            return ast_fail(reason, "函数返回类型错误");
        for (CCUINT32 i = 1; i <= node->as.func_decl.param_count; i++)
        {
            if (!ast_child_is_kind(ast, id, i, MK_AST_PARAM))
                return ast_fail(reason, "函数参数节点错误");
        }
        if (!ast_child_is_kind(
                ast, id, child_count - 1, MK_AST_BLOCK
            ))
            return ast_fail(reason, "函数体必须是 block");
        if (!ast_string_ok(ast, node->as.func_decl.name))
            return ast_fail(reason, "函数名无效");
        break;

    case MK_AST_PARAM:
        if (child_count != 1 ||
            !ast_child_is_kind(ast, id, 0, MK_AST_TYPE))
            return ast_fail(reason, "参数节点错误");
        if (!ast_string_ok(ast, node->as.param.name))
            return ast_fail(reason, "参数名无效");
        break;

    case MK_AST_TYPE:
        if ((unsigned int)node->as.type.kind >=
            (unsigned int)MK_TYPE_KIND_COUNT)
            return ast_fail(reason, "类型节点类型非法");
        if (node->as.type.kind == MK_TYPE_LIST)
        {
            if (child_count != 1 ||
                !ast_child_is_kind(ast, id, 0, MK_AST_TYPE))
                return ast_fail(reason, "list 类型缺少元素类型");
        }
        else if (child_count != 0)
            return ast_fail(reason, "标量类型不应有子节点");
        break;

    default:
        return ast_fail(reason, "未知节点类型");
    }
    return CCTRUE;
}

static CCBOOL ast_validate_stmt(
    const MKAST *ast, MKASTNodeId id, const MKASTNode *node,
    CCUINT32 child_count, const char **reason
)
{
    switch (node->kind)
    {
    case MK_AST_BLOCK:
    {
        CCBOOL top_level = node->as.block.is_top_level;
        for (CCUINT32 i = 0; i < child_count; i++)
        {
            MKASTNodeId child = ast_nth_child(ast, id, i);
            const MKASTNode *child_node = ast_node_at_const(ast, child);
            if (!child_node)
                return ast_fail(reason, "block 子节点无效");

            if (top_level)
            {
                if (child_node->kind == MK_AST_IF &&
                    child_node->as.if_stmt.is_top_level)
                    continue;
                return ast_fail(reason, "顶层 block 子节点类型非法");
            }

            if (child_node->kind == MK_AST_VAR_DECL)
                continue;
            if (!ast_is_normal_statement(ast, child))
                return ast_fail(reason, "普通 block 子节点类型非法");
        }
        break;
    }

    case MK_AST_EXPR_STMT:
        if (child_count != 1 || !ast_child_is_expression(ast, id, 0))
            return ast_fail(reason, "表达式语句节点错误");
        break;

    case MK_AST_IF:
    {
        if (node->as.if_stmt.has_else)
        {
            if (child_count != 3)
                return ast_fail(reason, "if 子节点数量错误");
        }
        else if (child_count != 2)
            return ast_fail(reason, "if 子节点数量错误");

        if (!ast_child_is_expression(ast, id, 0))
            return ast_fail(reason, "if 条件类型错误");

        if (node->as.if_stmt.is_top_level)
        {
            if (!ast_child_is_top_branch(ast, id, 1) ||
                (node->as.if_stmt.has_else &&
                 !ast_child_is_top_branch(ast, id, 2)))
                return ast_fail(reason, "顶层 if 分支类型错误");
        }
        else
        {
            if (!ast_child_is_normal_branch(ast, id, 1) ||
                (node->as.if_stmt.has_else &&
                 !ast_child_is_normal_branch(ast, id, 2)))
                return ast_fail(reason, "普通 if 分支类型错误");
        }
        break;
    }

    case MK_AST_WHILE:
        if (child_count != 2 ||
            !ast_child_is_expression(ast, id, 0) ||
            !ast_child_is_normal_branch(ast, id, 1))
            return ast_fail(reason, "while 节点错误");
        break;

    case MK_AST_FOR_IN:
        if (child_count != 3 ||
            !ast_child_is_kind(ast, id, 0, MK_AST_TYPE) ||
            !ast_child_is_expression(ast, id, 1) ||
            !ast_child_is_normal_branch(ast, id, 2))
            return ast_fail(reason, "for-in 节点错误");
        if (!ast_string_ok(ast, node->as.for_in.name))
            return ast_fail(reason, "for-in 变量名无效");
        break;

    case MK_AST_RETURN:
        if (node->as.return_stmt.has_value)
        {
            if (child_count != 1 ||
                !ast_child_is_expression(ast, id, 0))
                return ast_fail(reason, "return 返回值错误");
        }
        else if (child_count != 0)
            return ast_fail(reason, "return 不应有子节点");
        break;

    case MK_AST_BREAK:
    case MK_AST_CONTINUE:
        if (child_count != 0)
            return ast_fail(reason, "break/continue 不应有子节点");
        break;

    case MK_AST_ASSIGN:
        if (child_count != 2)
            return ast_fail(reason, "赋值子节点数量错误");
        if (!ast_child_is_kind(ast, id, 0, MK_AST_IDENT) &&
            !ast_child_is_kind(ast, id, 0, MK_AST_DISCARD))
            return ast_fail(reason, "赋值目标必须是 ident 或 discard");
        if (!ast_child_is_expression(ast, id, 1))
            return ast_fail(reason, "赋值值类型错误");
        if ((unsigned int)node->as.assign.op >=
            (unsigned int)MK_ASSIGN_OP_COUNT)
            return ast_fail(reason, "赋值运算符非法");
        break;

    default:
        return ast_fail(reason, "未知节点类型");
    }
    return CCTRUE;
}

static CCBOOL ast_validate_expr(
    const MKAST *ast, MKASTNodeId id, const MKASTNode *node,
    CCUINT32 child_count, const char **reason
)
{
    switch (node->kind)
    {
    case MK_AST_LITERAL:
        if (child_count != 0)
            return ast_fail(reason, "字面量不应有子节点");
        if ((unsigned int)node->as.literal.kind >=
            (unsigned int)MK_LITERAL_KIND_COUNT)
            return ast_fail(reason, "字面量类型非法");
        if (node->as.literal.kind == MK_LITERAL_STRING &&
            !ast_string_ok(ast, node->as.literal.value.string))
            return ast_fail(reason, "字符串字面量引用无效");
        break;

    case MK_AST_IDENT:
        if (child_count != 0)
            return ast_fail(reason, "ident 不应有子节点");
        if (!ast_string_ok(ast, node->as.ident.name))
            return ast_fail(reason, "ident 名无效");
        break;

    case MK_AST_DISCARD:
        if (child_count != 0)
            return ast_fail(reason, "discard 不应有子节点");
        break;

    case MK_AST_UNARY:
        if (child_count != 1 ||
            !ast_child_is_expression(ast, id, 0))
            return ast_fail(reason, "一元表达式节点错误");
        if ((unsigned int)node->as.unary.op >=
            (unsigned int)MK_UNARY_OP_COUNT)
            return ast_fail(reason, "一元运算符非法");
        break;

    case MK_AST_BINARY:
        if (child_count != 2 ||
            !ast_child_is_expression(ast, id, 0) ||
            !ast_child_is_expression(ast, id, 1))
            return ast_fail(reason, "二元表达式节点错误");
        if ((unsigned int)node->as.binary.op >=
            (unsigned int)MK_BINARY_OP_COUNT)
            return ast_fail(reason, "二元运算符非法");
        break;

    case MK_AST_CALL:
        if (!ast_children_all_expression(ast, id, child_count))
            return ast_fail(reason, "调用实参类型错误");
        if (!ast_string_ok(ast, node->as.call.callee))
            return ast_fail(reason, "调用名无效");
        break;

    case MK_AST_INDEX:
        if (child_count != 2 ||
            !ast_child_is_expression(ast, id, 0) ||
            !ast_child_is_expression(ast, id, 1))
            return ast_fail(reason, "索引表达式节点错误");
        break;

    case MK_AST_MEMBER:
        if (child_count != 1 ||
            !ast_child_is_expression(ast, id, 0))
            return ast_fail(reason, "成员表达式节点错误");
        if (!ast_string_ok(ast, node->as.member.member))
            return ast_fail(reason, "成员名无效");
        break;

    case MK_AST_LIST:
        if (!ast_children_all_expression(ast, id, child_count))
            return ast_fail(reason, "列表元素类型错误");
        break;

    case MK_AST_CONFIG_REF:
        if (child_count != 0)
            return ast_fail(reason, "配置引用不应有子节点");
        if (!ast_string_ok(ast, node->as.config_ref.path))
            return ast_fail(reason, "配置引用路径无效");
        break;

    default:
        return ast_fail(reason, "未知节点类型");
    }
    return CCTRUE;
}

static CCBOOL ast_validate_node(
    const MKAST *ast, MKASTNodeId id, const char **reason
)
{
    const MKASTNode *node = ast_node_at_const(ast, id);
    if (!node)
        return ast_fail(reason, "节点 ID 越界");
    if ((unsigned int)node->kind >= (unsigned int)MK_AST_KIND_COUNT)
        return ast_fail(reason, "节点类型非法");

    if (id == ast->root)
    {
        if (node->kind != MK_AST_MODULE)
            return ast_fail(reason, "根节点不是 module");
        if (node->parent != MK_AST_NODE_NIL)
            return ast_fail(reason, "根节点父链接非空");
    }
    else
    {
        if (node->kind == MK_AST_MODULE)
            return ast_fail(reason, "module 只能作为根节点");
        if (node->parent == MK_AST_NODE_NIL ||
            node->parent >= ast_node_count_internal(ast))
            return ast_fail(reason, "节点父链接无效");
    }

    CCUINT32 child_count = 0;
    if (!ast_check_children(ast, id, node, &child_count))
        return ast_fail(reason, "子节点链接错误");

    switch (node->kind)
    {
    case MK_AST_MODULE:
    case MK_AST_INCLUDE_DECL:
    case MK_AST_IMPORT_DECL:
    case MK_AST_IMPORT_ITEM:
    case MK_AST_VAR_DECL:
    case MK_AST_FUNC_DECL:
    case MK_AST_PARAM:
    case MK_AST_TYPE:
        return ast_validate_decl(
            ast, id, node, child_count, reason
        );
    case MK_AST_BLOCK:
    case MK_AST_EXPR_STMT:
    case MK_AST_IF:
    case MK_AST_WHILE:
    case MK_AST_FOR_IN:
    case MK_AST_RETURN:
    case MK_AST_BREAK:
    case MK_AST_CONTINUE:
    case MK_AST_ASSIGN:
        return ast_validate_stmt(
            ast, id, node, child_count, reason
        );
    default:
        return ast_validate_expr(
            ast, id, node, child_count, reason
        );
    }
}
CCBOOL mk_ast_validate(const MKAST *ast, const char **reason)
{
    if (reason)
        *reason = NULL;
    if (!ast)
        return ast_fail(reason, "AST 为空");
    if (ast->nodes.size % (CCUINT32)sizeof(MKASTNode) != 0)
        return ast_fail(reason, "节点数组损坏");
    if (ast->strings.size % (CCUINT32)sizeof(MKASTString) != 0)
        return ast_fail(reason, "字符串数组损坏");

    CCUINT32 count = ast_node_count_internal(ast);
    if (count == 0)
        return ast_fail(reason, "AST 没有节点");
    if (ast->root == MK_AST_NODE_NIL || ast->root >= count)
        return ast_fail(reason, "根节点 ID 无效");

    const MKASTNode *root = ast_node_at_const(ast, ast->root);
    if (!root || root->kind != MK_AST_MODULE)
        return ast_fail(reason, "根节点不是 module");
    if (root->parent != MK_AST_NODE_NIL)
        return ast_fail(reason, "根节点父链接非空");

    CCUINT8 *seen = ccalloc(NULL, count, MK_AST_COMMENT);
    if (!seen)
        return ast_fail(reason, "内存分配失败");
    memset(seen, 0, count);

    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    MKASTNodeId root_id = ast->root;
    if (!mk_dyn_push(
            &stack, MK_AST_ID_ALIGN, MK_AST_ID_SIZE, &root_id
        ))
    {
        ccalloc(seen, 0, NULL);
        return ast_fail(reason, "内存分配失败");
    }

    CCUINT32 visited = 0;
    while (stack.size > 0)
    {
        MKASTNodeId id = MK_AST_NODE_NIL;
        if (cc_dyn_core_pop(
                &stack, MK_AST_ID_ALIGN, MK_AST_ID_SIZE, &id
            ) != 0)
        {
            (void)ast_fail(reason, "节点栈损坏");
            goto Failed;
        }
        if (id >= count)
        {
            (void)ast_fail(reason, "节点 ID 越界");
            goto Failed;
        }
        if (seen[id])
        {
            (void)ast_fail(reason, "节点重复或存在环");
            goto Failed;
        }
        seen[id] = 1;
        visited++;

        if (!ast_validate_node(ast, id, reason))
            goto Failed;

        const MKASTNode *node = ast_node_at_const(ast, id);
        if (!node)
        {
            (void)ast_fail(reason, "节点 ID 越界");
            goto Failed;
        }
        MKASTNodeId child = node->first_child;
        CCUINT32 guard = 0;
        while (child != MK_AST_NODE_NIL)
        {
            const MKASTNode *child_node = ast_node_at_const(ast, child);
            if (!child_node || ++guard > count)
            {
                (void)ast_fail(reason, "子节点链接错误");
                goto Failed;
            }
            if (!mk_dyn_push(
                    &stack, MK_AST_ID_ALIGN, MK_AST_ID_SIZE, &child
                ))
            {
                (void)ast_fail(reason, "内存分配失败");
                goto Failed;
            }
            child = child_node->next_sibling;
        }
    }

    if (visited != count)
    {
        (void)ast_fail(reason, "存在不可达节点");
        goto Failed;
    }

    ccalloc(seen, 0, NULL);
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return CCTRUE;

Failed:
    ccalloc(seen, 0, NULL);
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return CCFALSE;
}

/* ============================================================================
 * 遍历
 * ========================================================================== */

CCBOOL mk_ast_visit(
    const MKAST *ast, MKASTNodeId root,
    MKASTVisitFn fn, void *user
)
{
    if (!ast || !fn || root == MK_AST_NODE_NIL)
        return CCFALSE;

    CCUINT32 node_count = ast_node_count_internal(ast);
    if (root >= node_count)
        return CCFALSE;

    CCDyn stack;
    CCDyn children;
    memset(&stack, 0, sizeof(stack));
    memset(&children, 0, sizeof(children));

    if (!mk_dyn_push(
            &stack, MK_AST_ID_ALIGN, MK_AST_ID_SIZE, &root
        ))
        goto Failed;

    CCUINT32 steps = 0;
    while (stack.size > 0)
    {
        MKASTNodeId id = MK_AST_NODE_NIL;
        if (cc_dyn_core_pop(
                &stack, MK_AST_ID_ALIGN, MK_AST_ID_SIZE, &id
            ) != 0)
            goto Failed;
        if (id >= node_count || ++steps > node_count)
            goto Failed;
        if (!fn(ast, id, user))
            goto Failed;

        children.size = 0;
        const MKASTNode *node = ast_node_at_const(ast, id);
        if (!node)
            goto Failed;
        MKASTNodeId child = node->first_child;
        CCUINT32 guard = 0;
        while (child != MK_AST_NODE_NIL)
        {
            const MKASTNode *child_node = ast_node_at_const(ast, child);
            if (!child_node || ++guard > node_count)
                goto Failed;
            if (!mk_dyn_push(
                    &children, MK_AST_ID_ALIGN, MK_AST_ID_SIZE, &child
                ))
                goto Failed;
            child = child_node->next_sibling;
        }

        CCUINT32 child_count = children.size / (CCUINT32)sizeof(MKASTNodeId);
        for (CCUINT32 i = child_count; i > 0; i--)
        {
            MKASTNodeId push_id = MK_AST_NODE_NIL;
            if (cc_dyn_core_seek(
                    &children, MK_AST_ID_ALIGN, MK_AST_ID_SIZE,
                    &push_id, i - 1
                ) != 0)
                goto Failed;
            if (!mk_dyn_push(
                    &stack, MK_AST_ID_ALIGN, MK_AST_ID_SIZE, &push_id
                ))
                goto Failed;
        }
    }

    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    if (children.array)
        ccalloc(children.array, 0, NULL);
    return CCTRUE;

Failed:
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    if (children.array)
        ccalloc(children.array, 0, NULL);
    return CCFALSE;
}
