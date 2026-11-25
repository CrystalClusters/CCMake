/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK AST 节点、字符串表、源码范围与遍历接口
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_MK_AST_H_
#define _INCLUDE_MK_AST_H_

#include "mk_source.h"

#include <cc_dyn.h>

typedef CCUINT32 MKASTNodeId;
typedef CCUINT32 MKASTStringId;

#define MK_AST_NODE_NIL   ((MKASTNodeId)0xFFFFFFFFu)
#define MK_AST_STRING_NIL ((MKASTStringId)0xFFFFFFFFu)

typedef enum {
    MK_AST_MODULE = 0,
    MK_AST_INCLUDE_DECL,
    MK_AST_IMPORT_DECL,
    MK_AST_IMPORT_ITEM,
    MK_AST_VAR_DECL,
    MK_AST_FUNC_DECL,
    MK_AST_PARAM,
    MK_AST_TYPE,

    MK_AST_BLOCK,
    MK_AST_EXPR_STMT,
    MK_AST_IF,
    MK_AST_WHILE,
    MK_AST_FOR_IN,
    MK_AST_RETURN,
    MK_AST_BREAK,
    MK_AST_CONTINUE,
    MK_AST_ASSIGN,

    MK_AST_LITERAL,
    MK_AST_IDENT,
    MK_AST_DISCARD,
    MK_AST_UNARY,
    MK_AST_BINARY,
    MK_AST_CALL,
    MK_AST_INDEX,
    MK_AST_MEMBER,
    MK_AST_LIST,
    MK_AST_CONFIG_REF,

    MK_AST_KIND_COUNT
} MKASTKind;

typedef enum {
    MK_TYPE_STRING = 0,
    MK_TYPE_INT,
    MK_TYPE_BOOL,
    MK_TYPE_FLOAT,
    MK_TYPE_LIST,
    MK_TYPE_VOID,
    MK_TYPE_KIND_COUNT
} MKTypeKind;

typedef enum {
    MK_LITERAL_INT = 0,
    MK_LITERAL_FLOAT,
    MK_LITERAL_STRING,
    MK_LITERAL_BOOL,
    MK_LITERAL_KIND_COUNT
} MKLiteralKind;

typedef enum {
    MK_UNARY_POS = 0,
    MK_UNARY_NEG,
    MK_UNARY_NOT,
    MK_UNARY_OP_COUNT
} MKUnaryOp;

typedef enum {
    MK_BINARY_ADD = 0,
    MK_BINARY_SUB,
    MK_BINARY_MUL,
    MK_BINARY_DIV,
    MK_BINARY_MOD,
    MK_BINARY_EQ,
    MK_BINARY_NE,
    MK_BINARY_LT,
    MK_BINARY_LE,
    MK_BINARY_GT,
    MK_BINARY_GE,
    MK_BINARY_AND,
    MK_BINARY_OR,
    MK_BINARY_OP_COUNT
} MKBinaryOp;

typedef enum {
    MK_ASSIGN_SET = 0,
    MK_ASSIGN_ADD,
    MK_ASSIGN_OP_COUNT
} MKAssignOp;

typedef enum {
    MK_IMPORT_FUNC = 0,
    MK_IMPORT_KIND_COUNT
} MKImportKind;

/* AST 拥有的字符串；data 末尾补 0，len 可包含 0 字节 */
typedef struct mk_ast_string {
    char *data;
    CCUINT32 len;
} MKASTString;

/*
 * AST 节点。
 * 子节点通过 first_child / next_sibling 串联；固定子节点顺序见设计文档。
 * 节点按值存放在 MKAST.nodes 中，外部只保存节点 ID；
 * 添加节点会使之前的节点指针失效。
 */
typedef struct mk_ast_node {
    MKASTKind kind;
    MKSourceSpan span;

    MKASTNodeId parent;
    MKASTNodeId first_child;
    MKASTNodeId last_child;
    MKASTNodeId next_sibling;

    union {
        struct {
            MKASTStringId name;
            CCBOOL is_const;
        } var_decl;

        struct {
            MKASTStringId name;
            CCUINT32 param_count;
        } func_decl;

        struct {
            MKImportKind kind;
            MKASTStringId module;
            MKASTStringId alias;
            CCBOOL wildcard;
        } import_decl;

        struct {
            MKASTStringId module;
        } include_decl;

        struct {
            MKASTStringId name;
            MKASTStringId alias;
        } import_item;

        struct {
            MKASTStringId name;
        } param;

        struct {
            MKTypeKind kind;
        } type;

        struct {
            MKASTStringId name;
        } ident;

        struct {
            MKASTStringId path;
        } config_ref;

        struct {
            MKLiteralKind kind;
            union {
                CCINT64 integer;
                double floating;
                CCBOOL boolean;
                MKASTStringId string;
            } value;
        } literal;

        struct {
            MKUnaryOp op;
        } unary;

        struct {
            MKBinaryOp op;
        } binary;

        struct {
            MKAssignOp op;
        } assign;

        struct {
            MKASTStringId callee;
        } call;

        struct {
            MKASTStringId member;
        } member;

        struct {
            CCBOOL is_top_level;
        } block;

        struct {
            CCBOOL has_else;
            CCBOOL is_top_level;
        } if_stmt;

        struct {
            MKASTStringId name;
        } for_in;

        struct {
            CCBOOL has_value;
        } return_stmt;
    } as;
} MKASTNode;

/*
 * AST 唯一所有者：节点数组、字符串表、文件路径副本。
 * 节点和字符串 ID 在 AST 生命周期内稳定。
 */
typedef struct mk_ast {
    char *file;
    CCDyn nodes;
    CCDyn strings;
    MKASTNodeId root;
} MKAST;

MKAST *mk_ast_create(const char *file);
void mk_ast_destroy(MKAST *ast);

/* 创建节点/字符串；失败返回 NIL */
MKASTNodeId mk_ast_add_node(
    MKAST *ast, MKASTKind kind, const MKSourceSpan *span
);
MKASTStringId mk_ast_add_string(
    MKAST *ast, const char *data, CCUINT32 len
);

/* 把尚未挂接的子节点追加到父节点末尾；失败返回 CCFALSE */
CCBOOL mk_ast_append_child(
    MKAST *ast, MKASTNodeId parent, MKASTNodeId child
);

/* 构建期使用；AST 发布后只使用 const 访问器 */
MKASTNode *mk_ast_node_mut(MKAST *ast, MKASTNodeId id);
const MKASTNode *mk_ast_node(const MKAST *ast, MKASTNodeId id);
const char *mk_ast_string(
    const MKAST *ast, MKASTStringId id, CCUINT32 *out_len
);

const char *mk_ast_file(const MKAST *ast);
MKASTNodeId mk_ast_root(const MKAST *ast);
CCUINT32 mk_ast_node_count(const MKAST *ast);
CCUINT32 mk_ast_string_count(const MKAST *ast);

const char *mk_ast_kind_name(MKASTKind kind);
CCBOOL mk_ast_kind_is_expression(MKASTKind kind);
CCBOOL mk_ast_kind_is_statement(MKASTKind kind);
CCBOOL mk_ast_kind_is_declaration(MKASTKind kind);

/* 校验发布前的 AST；失败时 *reason 指向静态字符串 */
CCBOOL mk_ast_validate(const MKAST *ast, const char **reason);

/* 先序遍历；visitor 返回 CCFALSE 时中断 */
typedef CCBOOL (*MKASTVisitFn)(
    const MKAST *ast, MKASTNodeId id, void *user
);

CCBOOL mk_ast_visit(
    const MKAST *ast, MKASTNodeId root,
    MKASTVisitFn fn, void *user
);

#endif
