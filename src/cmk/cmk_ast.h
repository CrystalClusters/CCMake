/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK AST 节点、字符串表、源码范围与遍历接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_AST_H_
#define _INCLUDE_CMK_AST_H_

#include "cmk_source.h"

#include <cc_dyn.h>

typedef CCUINT32 CMKASTNodeId;
typedef CCUINT32 CMKASTStringId;

#define CMK_AST_NODE_NIL   ((CMKASTNodeId)0xFFFFFFFFu)
#define CMK_AST_STRING_NIL ((CMKASTStringId)0xFFFFFFFFu)

typedef enum {
    CMK_AST_MODULE = 0,
    CMK_AST_INCLUDE_DECL,
    CMK_AST_IMPORT_DECL,
    CMK_AST_IMPORT_ITEM,
    CMK_AST_VAR_DECL,
    CMK_AST_FUNC_DECL,
    CMK_AST_PARAM,
    CMK_AST_TYPE,

    CMK_AST_BLOCK,
    CMK_AST_EXPR_STMT,
    CMK_AST_IF,
    CMK_AST_WHILE,
    CMK_AST_FOR_IN,
    CMK_AST_RETURN,
    CMK_AST_BREAK,
    CMK_AST_CONTINUE,
    CMK_AST_ASSIGN,

    CMK_AST_LITERAL,
    CMK_AST_IDENT,
    CMK_AST_DISCARD,
    CMK_AST_UNARY,
    CMK_AST_BINARY,
    CMK_AST_CALL,
    CMK_AST_INDEX,
    CMK_AST_MEMBER,
    CMK_AST_LIST,
    CMK_AST_CONFIG_REF,

    CMK_AST_KIND_COUNT
} CMKASTKind;

typedef enum {
    CMK_TYPE_STRING = 0,
    CMK_TYPE_INT,
    CMK_TYPE_BOOL,
    CMK_TYPE_FLOAT,
    CMK_TYPE_LIST,
    CMK_TYPE_VOID,
    CMK_TYPE_KIND_COUNT
} CMKTypeKind;

typedef enum {
    CMK_LITERAL_INT = 0,
    CMK_LITERAL_FLOAT,
    CMK_LITERAL_STRING,
    CMK_LITERAL_BOOL,
    CMK_LITERAL_KIND_COUNT
} CMKLiteralKind;

typedef enum {
    CMK_UNARY_POS = 0,
    CMK_UNARY_NEG,
    CMK_UNARY_NOT,
    CMK_UNARY_OP_COUNT
} CMKUnaryOp;

typedef enum {
    CMK_BINARY_ADD = 0,
    CMK_BINARY_SUB,
    CMK_BINARY_MUL,
    CMK_BINARY_DIV,
    CMK_BINARY_MOD,
    CMK_BINARY_EQ,
    CMK_BINARY_NE,
    CMK_BINARY_LT,
    CMK_BINARY_LE,
    CMK_BINARY_GT,
    CMK_BINARY_GE,
    CMK_BINARY_AND,
    CMK_BINARY_OR,
    CMK_BINARY_OP_COUNT
} CMKBinaryOp;

typedef enum {
    CMK_ASSIGN_SET = 0,
    CMK_ASSIGN_ADD,
    CMK_ASSIGN_OP_COUNT
} CMKAssignOp;

typedef enum {
    CMK_IMPORT_FUNC = 0,
    CMK_IMPORT_KIND_COUNT
} CMKImportKind;

/* AST 拥有的字符串；data 末尾补 0，len 可包含 0 字节 */
typedef struct cmk_ast_string {
    char *data;
    CCUINT32 len;
} CMKASTString;

/*
 * AST 节点。
 * 子节点通过 first_child / next_sibling 串联；固定子节点顺序见设计文档。
 * 节点按值存放在 CMKAST.nodes 中，外部只保存节点 ID；
 * 添加节点会使之前的节点指针失效。
 */
typedef struct cmk_ast_node {
    CMKASTKind kind;
    CMKSourceSpan span;

    CMKASTNodeId parent;
    CMKASTNodeId first_child;
    CMKASTNodeId last_child;
    CMKASTNodeId next_sibling;

    union {
        struct {
            CMKASTStringId name;
            CCBOOL is_const;
        } var_decl;

        struct {
            CMKASTStringId name;
            CCUINT32 param_count;
        } func_decl;

        struct {
            CMKImportKind kind;
            CMKASTStringId module;
            CMKASTStringId alias;
            CCBOOL wildcard;
        } import_decl;

        struct {
            CMKASTStringId module;
        } include_decl;

        struct {
            CMKASTStringId name;
            CMKASTStringId alias;
        } import_item;

        struct {
            CMKASTStringId name;
        } param;

        struct {
            CMKTypeKind kind;
        } type;

        struct {
            CMKASTStringId name;
        } ident;

        struct {
            CMKASTStringId path;
        } config_ref;

        struct {
            CMKLiteralKind kind;
            union {
                CCINT64 integer;
                double floating;
                CCBOOL boolean;
                CMKASTStringId string;
            } value;
        } literal;

        struct {
            CMKUnaryOp op;
        } unary;

        struct {
            CMKBinaryOp op;
        } binary;

        struct {
            CMKAssignOp op;
        } assign;

        struct {
            CMKASTStringId callee;
        } call;

        struct {
            CMKASTStringId member;
        } member;

        struct {
            CCBOOL is_top_level;
        } block;

        struct {
            CCBOOL has_else;
            CCBOOL is_top_level;
        } if_stmt;

        struct {
            CMKASTStringId name;
        } for_in;

        struct {
            CCBOOL has_value;
        } return_stmt;
    } as;
} CMKASTNode;

/*
 * AST 唯一所有者：节点数组、字符串表、文件路径副本。
 * 节点和字符串 ID 在 AST 生命周期内稳定。
 */
typedef struct cmk_ast {
    char *file;
    CCDyn nodes;
    CCDyn strings;
    CMKASTNodeId root;
} CMKAST;

CMKAST *cmk_ast_create(const char *file);
void cmk_ast_destroy(CMKAST *ast);

/* 创建节点/字符串；失败返回 NIL */
CMKASTNodeId cmk_ast_add_node(
    CMKAST *ast, CMKASTKind kind, const CMKSourceSpan *span
);
CMKASTStringId cmk_ast_add_string(
    CMKAST *ast, const char *data, CCUINT32 len
);

/* 把尚未挂接的子节点追加到父节点末尾；失败返回 CCFALSE */
CCBOOL cmk_ast_append_child(
    CMKAST *ast, CMKASTNodeId parent, CMKASTNodeId child
);

/* 构建期使用；AST 发布后只使用 const 访问器 */
CMKASTNode *cmk_ast_node_mut(CMKAST *ast, CMKASTNodeId id);
const CMKASTNode *cmk_ast_node(const CMKAST *ast, CMKASTNodeId id);
const char *cmk_ast_string(
    const CMKAST *ast, CMKASTStringId id, CCUINT32 *out_len
);

const char *cmk_ast_file(const CMKAST *ast);
CMKASTNodeId cmk_ast_root(const CMKAST *ast);
CCUINT32 cmk_ast_node_count(const CMKAST *ast);
CCUINT32 cmk_ast_string_count(const CMKAST *ast);

const char *cmk_ast_kind_name(CMKASTKind kind);
CCBOOL cmk_ast_kind_is_expression(CMKASTKind kind);
CCBOOL cmk_ast_kind_is_statement(CMKASTKind kind);
CCBOOL cmk_ast_kind_is_declaration(CMKASTKind kind);

/* 校验发布前的 AST；失败时 *reason 指向静态字符串 */
CCBOOL cmk_ast_validate(const CMKAST *ast, const char **reason);

/* 先序遍历；visitor 返回 CCFALSE 时中断 */
typedef CCBOOL (*CMKASTVisitFn)(
    const CMKAST *ast, CMKASTNodeId id, void *user
);

CCBOOL cmk_ast_visit(
    const CMKAST *ast, CMKASTNodeId root,
    CMKASTVisitFn fn, void *user
);

#endif
