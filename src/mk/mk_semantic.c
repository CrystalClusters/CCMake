/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 语义分析：作用域、类型、函数、内建与配置引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_semantic_internal.h"
#include "mk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdarg.h>
#include <string.h>

#define MK_SEM_COMMENT CC_DBG_REL_TAG(".mk 语义", NULL)

typedef enum mk_scope_add_result {
    MK_SCOPE_ADD_OK = 0,
    MK_SCOPE_ADD_DUPLICATE,
    MK_SCOPE_ADD_NOMEM
} MKScopeAddResult;

typedef struct mk_sem_flow {
    CCBOOL reachable;
    CCBOOL returns;
} MKSemFlow;

/* ============================================================================
 * AST 访问与诊断
 * ========================================================================== */

static const MKASTNode *sem_node(
    const MKSemanticModule *module, MKASTNodeId id
)
{
    if (!module || !module->ast)
        return NULL;
    return mk_ast_node(module->ast, id);
}

static MKASTNodeId sem_child(
    const MKSemanticModule *module, MKASTNodeId parent, CCUINT32 index
)
{
    const MKASTNode *node = sem_node(module, parent);
    if (!node)
        return MK_AST_NODE_NIL;
    MKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < index && child != MK_AST_NODE_NIL; i++)
    {
        const MKASTNode *child_node = sem_node(module, child);
        if (!child_node)
            return MK_AST_NODE_NIL;
        child = child_node->next_sibling;
    }
    return child;
}

static CCUINT32 sem_child_count(
    const MKSemanticModule *module, MKASTNodeId parent
)
{
    const MKASTNode *node = sem_node(module, parent);
    if (!node)
        return 0;
    CCUINT32 count = 0;
    MKASTNodeId child = node->first_child;
    while (child != MK_AST_NODE_NIL)
    {
        count++;
        const MKASTNode *child_node = sem_node(module, child);
        if (!child_node)
            break;
        child = child_node->next_sibling;
    }
    return count;
}

static CCBOOL sem_string(
    const MKSemanticModule *module, MKASTStringId id,
    const char **out, CCUINT32 *out_len
)
{
    if (!module || !module->ast)
        return CCFALSE;
    CCUINT32 len = 0;
    const char *text = mk_ast_string(module->ast, id, &len);
    if (!text)
        return CCFALSE;
    if (out)
        *out = text;
    if (out_len)
        *out_len = len;
    return CCTRUE;
}

static CCBOOL sem_error(
    MKSemanticModule *module, MKASTNodeId node,
    const char *fmt, ...
)
{
    if (!module || !module->diagnostic)
        return CCFALSE;
    if (mk_diagnostic_has(module->diagnostic))
        return CCFALSE;

    const MKASTNode *ast_node = sem_node(module, node);
    const char *file = module->path;
    CCUINT32 line = 0;
    CCUINT32 col = 0;
    if (ast_node)
    {
        file = mk_ast_file(module->ast);
        line = ast_node->span.line;
        col = ast_node->span.col;
    }
    if (!file)
        file = "(.mk)";

    va_list ap;
    va_start(ap, fmt);
    (void)mk_diagnostic_vset(
        module->diagnostic, MK_DIAG_ERROR, file, line, col, fmt, ap
    );
    va_end(ap);
    module->ok = CCFALSE;
    return CCFALSE;
}

static CCBOOL sem_type_format(
    const MKType *type, char *buf, CCUINT32 size
)
{
    return mk_type_format(type, buf, size);
}

/* ============================================================================
 * 作用域
 * ========================================================================== */

static MKScope *scope_create(MKScope *parent)
{
    MKScope *scope = ccalloc(NULL, sizeof(MKScope), MK_SEM_COMMENT);
    if (!scope)
        return NULL;
    memset(scope, 0, sizeof(*scope));
    scope->parent = parent;
    return scope;
}

static void scope_destroy(MKScope *scope)
{
    if (!scope)
        return;
    MKSymbol *symbols = (MKSymbol *)scope->symbols.array;
    CCUINT32 count =
        scope->symbols.size / (CCUINT32)sizeof(MKSymbol);
    for (CCUINT32 i = 0; i < count; i++)
        mk_string_clear(&symbols[i].name);
    if (scope->symbols.array)
        ccalloc(scope->symbols.array, 0, NULL);
    ccalloc(scope, 0, NULL);
}

static const MKSymbol *scope_find(
    const MKScope *scope, const char *name, CCUINT32 len
)
{
    for (const MKScope *current = scope;
         current;
         current = current->parent)
    {
        const MKSymbol *symbols = (const MKSymbol *)current->symbols.array;
        CCUINT32 count =
            current->symbols.size / (CCUINT32)sizeof(MKSymbol);
        for (CCUINT32 i = 0; i < count; i++)
        {
            const MKSymbol *symbol = &symbols[i];
            if (symbol->name.len == len &&
                (len == 0 ||
                 memcmp(symbol->name.data, name, len) == 0))
                return symbol;
        }
    }
    return NULL;
}

static MKScopeAddResult scope_add(
    MKScope *scope, const char *name, CCUINT32 len,
    MKSymbolKind kind, MKType type, CCBOOL is_const,
    const MKFunctionInfo *function
)
{
    if (!scope || !name)
        return MK_SCOPE_ADD_NOMEM;

    const MKSymbol *symbols = (const MKSymbol *)scope->symbols.array;
    CCUINT32 count = scope->symbols.size / (CCUINT32)sizeof(MKSymbol);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (symbols[i].name.len == len &&
            (len == 0 || memcmp(symbols[i].name.data, name, len) == 0))
            return MK_SCOPE_ADD_DUPLICATE;
    }

    MKSymbol symbol;
    memset(&symbol, 0, sizeof(symbol));
    if (!mk_string_set(&symbol.name, name, len))
        return MK_SCOPE_ADD_NOMEM;
    symbol.kind = kind;
    symbol.type = type;
    symbol.is_const = is_const;
    symbol.function = function;

    if (!mk_dyn_push(
            &scope->symbols, (CCUINT32)sizeof(MKSymbol),
            (CCINT32)sizeof(MKSymbol), &symbol
        ))
    {
        mk_string_clear(&symbol.name);
        return MK_SCOPE_ADD_NOMEM;
    }
    return MK_SCOPE_ADD_OK;
}

/* ============================================================================
 * 类型
 * ========================================================================== */

static CCBOOL sem_type_from_ast(
    MKSemanticModule *module, MKASTNodeId type_node,
    MKType *out
)
{
    const MKASTNode *node = sem_node(module, type_node);
    if (!node || node->kind != MK_AST_TYPE)
        return sem_error(module, type_node, "内部错误：期望类型节点");

    switch (node->as.type.kind)
    {
    case MK_TYPE_VOID:
        *out = mk_type_scalar(MK_VALUE_VOID);
        return CCTRUE;

    case MK_TYPE_INT:
        *out = mk_type_scalar(MK_VALUE_INT);
        return CCTRUE;

    case MK_TYPE_FLOAT:
        *out = mk_type_scalar(MK_VALUE_FLOAT);
        return CCTRUE;

    case MK_TYPE_BOOL:
        *out = mk_type_scalar(MK_VALUE_BOOL);
        return CCTRUE;

    case MK_TYPE_STRING:
        *out = mk_type_scalar(MK_VALUE_STRING);
        return CCTRUE;

    case MK_TYPE_LIST:
    {
        MKASTNodeId element_id = sem_child(module, type_node, 0);
        MKType element;
        if (!sem_type_from_ast(
                module, element_id, &element
            ))
            return CCFALSE;
        if (element.kind == MK_VALUE_LIST)
            return sem_error(module, type_node, "不允许嵌套 list 类型");
        if (element.kind == MK_VALUE_VOID)
            return sem_error(module, type_node, "list 元素类型不能是 void");
        *out = mk_type_list(element.kind);
        return CCTRUE;
    }

    default:
        return sem_error(module, type_node, "未知的类型节点");
    }
}

static CCBOOL sem_type_expect(
    MKSemanticModule *module, MKASTNodeId node,
    const MKType *actual, const MKType *expected,
    const char *context
)
{
    if (mk_type_equal(actual, expected))
        return CCTRUE;

    char actual_buf[64];
    char expected_buf[64];
    sem_type_format(actual, actual_buf, sizeof(actual_buf));
    sem_type_format(expected, expected_buf, sizeof(expected_buf));
    return sem_error(
        module, node, "%s 类型不匹配：需要 %s，实际 %s",
        context, expected_buf, actual_buf
    );
}

/* ============================================================================
 * 生命周期
 * ========================================================================== */

const MKAST *mk_semantic_ast(const MKSemanticModule *module)
{
    return module ? module->ast : NULL;
}

const char *mk_semantic_path(const MKSemanticModule *module)
{
    return module ? module->path : NULL;
}

MKSemanticModule *mk_semantic_module_create(
    MKAST *ast, const char *path,
    MKConfig *config, MKBuiltinRegistry *builtins,
    MKDiagnostic *diagnostic
)
{
    if (!ast || !path || !config || !builtins || !diagnostic)
        return NULL;

    MKSemanticModule *module =
        ccalloc(NULL, sizeof(MKSemanticModule), MK_SEM_COMMENT);
    if (!module)
        return NULL;
    memset(module, 0, sizeof(*module));

    size_t path_len = strlen(path);
    module->path = ccalloc(
        NULL, (CCUINT64)path_len + 1, MK_SEM_COMMENT
    );
    if (!module->path)
    {
        mk_semantic_module_destroy(module);
        return NULL;
    }
    memcpy(module->path, path, path_len + 1);

    module->ast = ast;
    module->config = config;
    module->builtins = builtins;
    module->diagnostic = diagnostic;
    module->node_count = mk_ast_node_count(ast);
    if (module->node_count > 0)
    {
        module->nodes = ccalloc(
            NULL,
            (CCUINT64)module->node_count * sizeof(MKNodeSemantics),
            MK_SEM_COMMENT
        );
        if (!module->nodes)
        {
            mk_semantic_module_destroy(module);
            return NULL;
        }
        memset(
            module->nodes, 0,
            (size_t)module->node_count * sizeof(MKNodeSemantics)
        );
    }
    return module;
}

static void function_info_destroy(MKFunctionInfo *function)
{
    if (!function)
        return;
    mk_string_clear(&function->name);
    if (function->params)
        ccalloc(function->params, 0, NULL);
    ccalloc(function, 0, NULL);
}

void mk_semantic_module_destroy(MKSemanticModule *module)
{
    if (!module)
        return;

    MKFunctionInfo **functions =
        (MKFunctionInfo **)module->functions.array;
    CCUINT32 function_count =
        module->functions.size / (CCUINT32)sizeof(MKFunctionInfo *);
    for (CCUINT32 i = 0; i < function_count; i++)
        function_info_destroy(functions[i]);
    if (module->functions.array)
        ccalloc(module->functions.array, 0, NULL);

    MKImportedFunction **imported_functions =
        (MKImportedFunction **)module->imported_functions.array;
    CCUINT32 imported_function_count =
        module->imported_functions.size /
        (CCUINT32)sizeof(MKImportedFunction *);
    for (CCUINT32 i = 0; i < imported_function_count; i++)
    {
        MKImportedFunction *record = imported_functions[i];
        if (!record)
            continue;
        mk_string_clear(&record->name);
        ccalloc(record, 0, NULL);
    }
    if (module->imported_functions.array)
        ccalloc(module->imported_functions.array, 0, NULL);

    if (module->module_scope)
        scope_destroy(module->module_scope);
    if (module->nodes)
        ccalloc(module->nodes, 0, NULL);
    if (module->path)
        ccalloc(module->path, 0, NULL);
    ccalloc(module, 0, NULL);
}

CCBOOL mk_semantic_add_imported_function(
    MKSemanticModule *module,
    const char *name, CCUINT32 name_len,
    const MKFunctionInfo *function
)
{
    if (!module || !name || !function)
        return CCFALSE;
    MKImportedFunction *record =
        ccalloc(NULL, sizeof(MKImportedFunction), MK_SEM_COMMENT);
    if (!record)
        return CCFALSE;
    memset(record, 0, sizeof(*record));
    if (!mk_string_set(&record->name, name, name_len))
    {
        ccalloc(record, 0, NULL);
        return CCFALSE;
    }
    record->function = function;
    if (!mk_dyn_push(
            &module->imported_functions,
            (CCUINT32)sizeof(MKImportedFunction *),
            (CCINT32)sizeof(MKImportedFunction *), &record
        ))
    {
        mk_string_clear(&record->name);
        ccalloc(record, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

/* ============================================================================
 * 预声明
 * ========================================================================== */

static CCBOOL sem_predeclare_function(
    MKSemanticModule *module, MKASTNodeId decl
)
{
    const MKASTNode *node = sem_node(module, decl);
    if (!node || node->kind != MK_AST_FUNC_DECL)
        return sem_error(module, decl, "内部错误：期望函数声明");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.func_decl.name, &name, &name_len))
        return sem_error(module, decl, "函数名无效");

    if (mk_builtin_find(module->builtins, name, name_len))
        return sem_error(
            module, decl, "用户函数不能与内建函数同名：%.*s",
            (int)name_len, name
        );

    CCUINT32 child_count = sem_child_count(module, decl);
    if (node->as.func_decl.param_count + 2 != child_count)
        return sem_error(module, decl, "函数声明子节点数量错误");

    MKFunctionInfo *function =
        ccalloc(NULL, sizeof(MKFunctionInfo), MK_SEM_COMMENT);
    if (!function)
        return sem_error(module, decl, "内存分配失败");
    memset(function, 0, sizeof(*function));
    function->decl = decl;
    function->module = module;
    if (!mk_string_set(&function->name, name, name_len))
    {
        function_info_destroy(function);
        return sem_error(module, decl, "内存分配失败");
    }

    MKASTNodeId return_type_id = sem_child(module, decl, 0);
    if (!sem_type_from_ast(
            module, return_type_id, &function->return_type
        ))
    {
        function_info_destroy(function);
        return CCFALSE;
    }

    CCUINT32 param_count = node->as.func_decl.param_count;
    if (param_count > 0)
    {
        function->params = ccalloc(
            NULL,
            (CCUINT64)param_count * sizeof(MKType),
            MK_SEM_COMMENT
        );
        if (!function->params)
        {
            function_info_destroy(function);
            return sem_error(module, decl, "内存分配失败");
        }
        memset(
            function->params, 0,
            (size_t)param_count * sizeof(MKType)
        );
    }
    function->param_count = param_count;

    for (CCUINT32 i = 0; i < param_count; i++)
    {
        MKASTNodeId param = sem_child(module, decl, i + 1);
        const MKASTNode *param_node = sem_node(module, param);
        if (!param_node || param_node->kind != MK_AST_PARAM)
        {
            function_info_destroy(function);
            return sem_error(module, decl, "函数参数节点错误");
        }
        MKASTNodeId param_type = sem_child(module, param, 0);
        if (!sem_type_from_ast(
                module, param_type, &function->params[i]
            ))
        {
            function_info_destroy(function);
            return CCFALSE;
        }
        if (function->params[i].kind == MK_VALUE_VOID)
        {
            function_info_destroy(function);
            return sem_error(module, param, "函数参数类型不能是 void");
        }

        const char *param_name = NULL;
        CCUINT32 param_name_len = 0;
        if (!sem_string(
                module, param_node->as.param.name,
                &param_name, &param_name_len
            ))
        {
            function_info_destroy(function);
            return sem_error(module, param, "参数名无效");
        }
        for (CCUINT32 j = 0; j < i; j++)
        {
            MKASTNodeId previous = sem_child(module, decl, j + 1);
            const MKASTNode *previous_node = sem_node(module, previous);
            const char *previous_name = NULL;
            CCUINT32 previous_len = 0;
            if (previous_node &&
                sem_string(
                    module, previous_node->as.param.name,
                    &previous_name, &previous_len
                ) &&
                previous_len == param_name_len &&
                memcmp(previous_name, param_name, param_name_len) == 0)
            {
                function_info_destroy(function);
                return sem_error(
                    module, param, "函数参数重名：%.*s",
                    (int)param_name_len, param_name
                );
            }
        }
    }

    if (!mk_dyn_push(
            &module->functions,
            (CCUINT32)sizeof(MKFunctionInfo *),
            (CCINT32)sizeof(MKFunctionInfo *), &function
        ))
    {
        function_info_destroy(function);
        return sem_error(module, decl, "内存分配失败");
    }

    MKScopeAddResult added = scope_add(
        module->module_scope, name, name_len, MK_SYMBOL_FUNCTION,
        function->return_type, CCFALSE, function
    );
    if (added != MK_SCOPE_ADD_OK)
    {
        return sem_error(
            module, decl,
            added == MK_SCOPE_ADD_DUPLICATE
                ? "重复的函数声明：%.*s"
                : "内存分配失败",
            (int)name_len, name
        );
    }
    return CCTRUE;
}

static CCBOOL sem_predeclare_global(
    MKSemanticModule *module, MKASTNodeId decl
)
{
    const MKASTNode *node = sem_node(module, decl);
    if (!node || node->kind != MK_AST_VAR_DECL)
        return sem_error(module, decl, "内部错误：期望变量声明");

    MKType type;
    if (!sem_type_from_ast(
            module, sem_child(module, decl, 0), &type
        ))
        return CCFALSE;
    if (type.kind == MK_VALUE_VOID)
        return sem_error(module, decl, "变量类型不能是 void");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.var_decl.name, &name, &name_len))
        return sem_error(module, decl, "变量名无效");

    MKScopeAddResult added = scope_add(
        module->module_scope, name, name_len, MK_SYMBOL_VAR,
        type, node->as.var_decl.is_const, NULL
    );
    if (added != MK_SCOPE_ADD_OK)
    {
        return sem_error(
            module, decl,
            added == MK_SCOPE_ADD_DUPLICATE
                ? "重复的变量声明：%.*s"
                : "内存分配失败",
            (int)name_len, name
        );
    }
    return CCTRUE;
}

/* ============================================================================
 * 表达式
 * ========================================================================== */

static CCBOOL sem_expr(
    MKSemanticModule *module, MKASTNodeId id,
    MKScope *scope, const MKType *expected,
    MKType *out
);

static void sem_set_type(
    MKSemanticModule *module, MKASTNodeId id, const MKType *type
)
{
    if (!module || !module->nodes || id >= module->node_count || !type)
        return;
    module->nodes[id].type = *type;
}

static CCBOOL sem_expr_literal(
    MKSemanticModule *module, MKASTNodeId id, MKType *out
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node || node->kind != MK_AST_LITERAL)
        return sem_error(module, id, "内部错误：期望字面量");

    switch (node->as.literal.kind)
    {
    case MK_LITERAL_INT:
        *out = mk_type_scalar(MK_VALUE_INT);
        return CCTRUE;

    case MK_LITERAL_FLOAT:
        *out = mk_type_scalar(MK_VALUE_FLOAT);
        return CCTRUE;

    case MK_LITERAL_STRING:
        *out = mk_type_scalar(MK_VALUE_STRING);
        return CCTRUE;

    case MK_LITERAL_BOOL:
        *out = mk_type_scalar(MK_VALUE_BOOL);
        return CCTRUE;

    default:
        return sem_error(module, id, "未知字面量类型");
    }
}

static CCBOOL sem_expr_config(
    MKSemanticModule *module, MKASTNodeId id,
    const MKType *expected, MKType *out
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node || node->kind != MK_AST_CONFIG_REF)
        return sem_error(module, id, "内部错误：期望配置引用");

    const char *path = NULL;
    CCUINT32 path_len = 0;
    if (!sem_string(module, node->as.config_ref.path, &path, &path_len))
        return sem_error(module, id, "配置引用路径无效");

    MKType type;
    MKConfigResult result = mk_config_type(module->config, path, &type);
    switch (result)
    {
    case MK_CONFIG_OK:
        break;

    case MK_CONFIG_MISSING:
        return sem_error(module, id, "配置字段不存在：%s", path);

    case MK_CONFIG_MIXED:
        return sem_error(module, id, "配置数组元素类型不一致：%s", path);

    case MK_CONFIG_NOMEM:
        return sem_error(module, id, "配置查询内存分配失败");

    default:
        return sem_error(
            module, id, "配置字段不是可用的标量或同质数组：%s", path
        );
    }

    if (type.kind == MK_VALUE_LIST &&
        type.element == MK_VALUE_VOID &&
        expected && expected->kind == MK_VALUE_LIST)
    {
        type.element = expected->element;
    }
    *out = type;
    return CCTRUE;
}


static CCBOOL sem_expr_ident(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    MKType *out
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node || node->kind != MK_AST_IDENT)
        return sem_error(module, id, "内部错误：期望标识符");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.ident.name, &name, &name_len))
        return sem_error(module, id, "标识符无效");

    const MKSymbol *symbol = scope_find(scope, name, name_len);
    if (symbol)
    {
        if (symbol->kind == MK_SYMBOL_FUNCTION)
            return sem_error(module, id, "函数名不能作为值使用：%s", name);
        *out = symbol->type;
        return CCTRUE;
    }

    return sem_error(module, id, "未定义的标识符：%s", name);
}

typedef enum mk_sem_expr_kind {
    MK_SEM_EXPR_LITERAL = 0,
    MK_SEM_EXPR_IDENT,
    MK_SEM_EXPR_CONFIG,
    MK_SEM_EXPR_MEMBER,
    MK_SEM_EXPR_LIST,
    MK_SEM_EXPR_UNARY,
    MK_SEM_EXPR_BINARY,
    MK_SEM_EXPR_CALL,
    MK_SEM_EXPR_INDEX
} MKSemExprKind;

typedef enum mk_sem_expr_stage {
    MK_SEM_EXPR_START = 0,
    MK_SEM_EXPR_LIST_ELEM,
    MK_SEM_EXPR_UNARY_OPERAND,
    MK_SEM_EXPR_BINARY_LEFT,
    MK_SEM_EXPR_BINARY_RIGHT,
    MK_SEM_EXPR_CALL_ARG,
    MK_SEM_EXPR_INDEX_BASE,
    MK_SEM_EXPR_INDEX_INDEX
} MKSemExprStage;

typedef struct mk_sem_expr_frame {
    MKSemExprKind kind;
    MKSemExprStage stage;
    MKASTNodeId id;
    MKScope *scope;
    MKType expected;
    CCBOOL has_expected;
    MKType out;
    CCUINT32 index;
    CCUINT32 count;
    MKType child_expected;
    CCBOOL child_has_expected;
    MKType left_type;
    MKValueType element;
    MKBinaryOp op;
    const MKFunctionInfo *function;
    const MKBuiltinSpec *builtin;
    CCUINT32 formal_count;
} MKSemExprFrame;

static MKSemExprFrame *sem_expr_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(MKSemExprFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKSemExprFrame);
    return &((MKSemExprFrame *)frames->array)[count - 1];
}

static CCBOOL sem_expr_push(
    MKSemanticModule *module, CCDyn *frames,
    MKASTNodeId id, MKScope *scope,
    const MKType *expected
)
{
    MKSemExprFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = MK_SEM_EXPR_LITERAL;
    frame.stage = MK_SEM_EXPR_START;
    frame.id = id;
    frame.scope = scope;
    if (expected)
    {
        frame.expected = *expected;
        frame.has_expected = CCTRUE;
    }
    if (!mk_dyn_push(
            frames, (CCUINT32)sizeof(MKSemExprFrame),
            (CCINT32)sizeof(MKSemExprFrame), &frame
        ))
        return sem_error(module, id, "内存分配失败");
    return CCTRUE;
}

static CCBOOL sem_expr_pop(CCDyn *frames, MKSemExprFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(MKSemExprFrame),
        (CCINT32)sizeof(MKSemExprFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void sem_expr_frames_abort(CCDyn *frames)
{
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL sem_expr_list_next(
    MKSemanticModule *module, MKSemExprFrame *frame,
    CCDyn *frames
)
{
    if (frame->index >= frame->count)
    {
        frame->out = mk_type_list(frame->element);
        return CCTRUE;
    }

    MKASTNodeId item = sem_child(module, frame->id, frame->index);
    MKType expected;
    const MKType *expected_ptr = NULL;
    if (frame->element != MK_VALUE_VOID)
    {
        expected = mk_type_scalar(frame->element);
        expected_ptr = &expected;
    }
    return sem_expr_push(
        module, frames, item, frame->scope,
        expected_ptr
    );
}

static CCBOOL sem_expr_call_next(
    MKSemanticModule *module, MKSemExprFrame *frame,
    CCDyn *frames
)
{
    if (frame->index >= frame->count)
    {
        if (frame->function)
        {
            frame->out = frame->function->return_type;
            module->nodes[frame->id].function = frame->function;
        }
        else
        {
            frame->out = frame->builtin->return_type;
            module->nodes[frame->id].builtin = frame->builtin;
            if (frame->builtin->flags & MK_BUILTIN_NORETURN)
                module->nodes[frame->id].noreturn = CCTRUE;
        }
        return CCTRUE;
    }

    MKASTNodeId arg = sem_child(module, frame->id, frame->index);
    MKType formal;
    const MKType *expected = NULL;
    if (frame->function)
    {
        formal = frame->function->params[frame->index];
        expected = &formal;
    }
    else if (frame->index < frame->formal_count)
    {
        formal = frame->builtin->args[frame->index].type;
        expected = &formal;
    }
    return sem_expr_push(module, frames, arg, frame->scope, expected);
}

static CCBOOL sem_expr_begin_call(
    MKSemanticModule *module, MKSemExprFrame *frame,
    const MKASTNode *node
)
{
    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.call.callee, &name, &name_len))
        return sem_error(module, frame->id, "函数名无效");

    const MKSymbol *symbol = scope_find(frame->scope, name, name_len);
    if (symbol && symbol->kind == MK_SYMBOL_VAR)
        return sem_error(
            module, frame->id, "变量不是函数：%.*s",
            (int)name_len, name
        );

    frame->count = sem_child_count(module, frame->id);
    frame->index = 0;
    if (symbol && symbol->kind == MK_SYMBOL_FUNCTION)
    {
        const MKFunctionInfo *function = symbol->function;
        if (!function)
            return sem_error(module, frame->id, "函数符号损坏");
        if (function->param_count != frame->count)
            return sem_error(
                module, frame->id,
                "函数 %.*s 需要 %u 个参数，实际 %u",
                (int)name_len, name, function->param_count, frame->count
            );
        frame->function = function;
        frame->stage = MK_SEM_EXPR_CALL_ARG;
        return CCTRUE;
    }

    const MKBuiltinSpec *spec =
        mk_builtin_find(module->builtins, name, name_len);
    if (!spec)
        return sem_error(
            module, frame->id, "未定义的函数：%.*s", (int)name_len, name
        );
    if (frame->count < spec->min_args ||
        (!(spec->flags & MK_BUILTIN_VARIADIC) &&
         frame->count > spec->max_args))
        return sem_error(
            module, frame->id,
            "内建函数调用参数数量错误：%s", spec->signature
        );
    frame->builtin = spec;
    frame->formal_count =
        (spec->flags & MK_BUILTIN_VARIADIC)
            ? spec->min_args : spec->max_args;
    frame->stage = MK_SEM_EXPR_CALL_ARG;
    return CCTRUE;
}

static CCBOOL sem_expr_apply_binary(
    MKSemanticModule *module, MKSemExprFrame *frame,
    const MKType *left_type, const MKType *right_type,
    MKType *out
)
{
    MKBinaryOp op = frame->op;
    if (op == MK_BINARY_AND || op == MK_BINARY_OR)
    {
        MKType bool_type = mk_type_scalar(MK_VALUE_BOOL);
        if (!sem_type_expect(
                module, sem_child(module, frame->id, 0),
                left_type, &bool_type, "逻辑运算"
            ) ||
            !sem_type_expect(
                module, sem_child(module, frame->id, 1),
                right_type, &bool_type, "逻辑运算"
            ))
            return CCFALSE;
        *out = bool_type;
        return CCTRUE;
    }

    if (op == MK_BINARY_EQ || op == MK_BINARY_NE ||
        op == MK_BINARY_LT || op == MK_BINARY_LE ||
        op == MK_BINARY_GT || op == MK_BINARY_GE)
    {
        if (!mk_type_equal(left_type, right_type))
            return sem_error(module, frame->id, "比较运算两侧类型不一致");
        if (left_type->kind == MK_VALUE_VOID ||
            left_type->kind == MK_VALUE_LIST)
            return sem_error(module, frame->id, "该类型不能参与比较");
        if (op != MK_BINARY_EQ && op != MK_BINARY_NE &&
            left_type->kind != MK_VALUE_INT &&
            left_type->kind != MK_VALUE_FLOAT &&
            left_type->kind != MK_VALUE_STRING)
            return sem_error(module, frame->id, "该类型不能参与大小比较");
        *out = mk_type_scalar(MK_VALUE_BOOL);
        return CCTRUE;
    }

    if (op == MK_BINARY_ADD)
    {
        if (left_type->kind == MK_VALUE_INT &&
            right_type->kind == MK_VALUE_INT)
        {
            *out = mk_type_scalar(MK_VALUE_INT);
            return CCTRUE;
        }
        if (left_type->kind == MK_VALUE_FLOAT &&
            right_type->kind == MK_VALUE_FLOAT)
        {
            *out = mk_type_scalar(MK_VALUE_FLOAT);
            return CCTRUE;
        }
        if (left_type->kind == MK_VALUE_STRING &&
            right_type->kind == MK_VALUE_STRING)
        {
            *out = mk_type_scalar(MK_VALUE_STRING);
            return CCTRUE;
        }
        if (left_type->kind == MK_VALUE_LIST &&
            right_type->kind == MK_VALUE_LIST)
        {
            MKValueType element = left_type->element;
            if (element == MK_VALUE_VOID)
                element = right_type->element;
            if (element == MK_VALUE_VOID)
                return sem_error(
                    module, frame->id, "空列表无法推导元素类型"
                );
            if (left_type->element != MK_VALUE_VOID &&
                right_type->element != MK_VALUE_VOID &&
                left_type->element != right_type->element)
                return sem_error(
                    module, frame->id, "list 拼接元素类型不一致"
                );
            *out = mk_type_list(element);
            return CCTRUE;
        }
        return sem_error(module, frame->id, "加法两侧类型不支持");
    }

    if (!mk_type_is_numeric(left_type) ||
        !mk_type_is_numeric(right_type) ||
        left_type->kind != right_type->kind)
        return sem_error(module, frame->id, "算术运算需要同类型数值");
    if (op == MK_BINARY_MOD && left_type->kind != MK_VALUE_INT)
        return sem_error(module, frame->id, "取模运算只支持 int");
    *out = *left_type;
    return CCTRUE;
}

static CCBOOL sem_expr_step_start(
    MKSemanticModule *module, MKSemExprFrame *frame, CCDyn *frames,
    const MKType *input, MKType *out, CCBOOL *done
)
{
    (void)input;
    const MKASTNode *node = sem_node(module, frame->id);
    if (!node)
        return sem_error(module, frame->id, "表达式节点缺失");

    switch (node->kind)
    {
    case MK_AST_LITERAL:
        *done = CCTRUE;
        return sem_expr_literal(module, frame->id, out);

    case MK_AST_IDENT:
        *done = CCTRUE;
        return sem_expr_ident(
            module, frame->id, frame->scope, out
        );

    case MK_AST_CONFIG_REF:
        *done = CCTRUE;
        return sem_expr_config(
            module, frame->id,
            frame->has_expected ? &frame->expected : NULL, out
        );

    case MK_AST_LIST:
        frame->kind = MK_SEM_EXPR_LIST;
        frame->count = sem_child_count(module, frame->id);
        frame->index = 0;
        frame->element =
            (frame->has_expected &&
             frame->expected.kind == MK_VALUE_LIST)
                ? frame->expected.element : MK_VALUE_VOID;
        frame->stage = MK_SEM_EXPR_LIST_ELEM;
        if (!sem_expr_list_next(module, frame, frames))
            return CCFALSE;
        if (frame->index >= frame->count)
        {
            *out = frame->out;
            *done = CCTRUE;
        }
        else
        {
            *done = CCFALSE;
        }
        return CCTRUE;

    case MK_AST_UNARY:
    {
        frame->kind = MK_SEM_EXPR_UNARY;
        frame->stage = MK_SEM_EXPR_UNARY_OPERAND;
        MKASTNodeId operand = sem_child(module, frame->id, 0);
        MKType expected;
        const MKType *expected_ptr = NULL;
        if (node->as.unary.op == MK_UNARY_NOT)
        {
            expected = mk_type_scalar(MK_VALUE_BOOL);
            expected_ptr = &expected;
        }
        else if (frame->has_expected &&
                 mk_type_is_numeric(&frame->expected))
        {
            expected = frame->expected;
            expected_ptr = &expected;
        }
        if (expected_ptr)
        {
            frame->child_expected = *expected_ptr;
            frame->child_has_expected = CCTRUE;
        }
        if (!sem_expr_push(
                module, frames, operand, frame->scope,
                expected_ptr
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    case MK_AST_BINARY:
    {
        frame->kind = MK_SEM_EXPR_BINARY;
        frame->op = node->as.binary.op;
        frame->stage = MK_SEM_EXPR_BINARY_LEFT;
        MKType expected;
        const MKType *expected_ptr = NULL;
        if (frame->op == MK_BINARY_AND || frame->op == MK_BINARY_OR)
        {
            expected = mk_type_scalar(MK_VALUE_BOOL);
            expected_ptr = &expected;
        }
        else if (frame->op != MK_BINARY_EQ &&
                 frame->op != MK_BINARY_NE &&
                 frame->op != MK_BINARY_LT &&
                 frame->op != MK_BINARY_LE &&
                 frame->op != MK_BINARY_GT &&
                 frame->op != MK_BINARY_GE &&
                 frame->has_expected &&
                 frame->expected.kind != MK_VALUE_VOID)
        {
            expected = frame->expected;
            expected_ptr = &expected;
        }
        if (expected_ptr)
        {
            frame->child_expected = *expected_ptr;
            frame->child_has_expected = CCTRUE;
        }
        if (!sem_expr_push(
                module, frames, sem_child(module, frame->id, 0),
                frame->scope, expected_ptr
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    case MK_AST_CALL:
        frame->kind = MK_SEM_EXPR_CALL;
        frame->stage = MK_SEM_EXPR_CALL_ARG;
        if (!sem_expr_begin_call(module, frame, node))
            return CCFALSE;
        if (!sem_expr_call_next(module, frame, frames))
            return CCFALSE;
        if (frame->index >= frame->count)
        {
            *out = frame->out;
            *done = CCTRUE;
        }
        else
        {
            *done = CCFALSE;
        }
        return CCTRUE;

    case MK_AST_INDEX:
        frame->kind = MK_SEM_EXPR_INDEX;
        frame->stage = MK_SEM_EXPR_INDEX_BASE;
        if (!sem_expr_push(
                module, frames, sem_child(module, frame->id, 0),
                frame->scope, NULL
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;

    default:
        return sem_error(
            module, frame->id,
            "不支持的表达式节点：%s",
            mk_ast_kind_name(node->kind)
        );
    }
}

static CCBOOL sem_expr_step_resume(
    MKSemanticModule *module, MKSemExprFrame *frame, CCDyn *frames,
    const MKType *input, MKType *out, CCBOOL *done
)
{
    switch (frame->kind)
    {
case MK_SEM_EXPR_LIST:
{
    MKASTNodeId item = sem_child(module, frame->id, frame->index);
    if (input->kind == MK_VALUE_LIST)
        return sem_error(module, item, "不允许嵌套 list");
    if (input->kind == MK_VALUE_VOID)
        return sem_error(module, item, "list 元素不能是 void");
    if (frame->element == MK_VALUE_VOID)
        frame->element = input->kind;
    else if (frame->element != input->kind)
        return sem_error(module, item, "list 元素类型不一致");
    frame->index++;
    if (frame->index >= frame->count)
    {
        *out = mk_type_list(frame->element);
        *done = CCTRUE;
        return CCTRUE;
    }
    if (!sem_expr_list_next(module, frame, frames))
        return CCFALSE;
    *done = CCFALSE;
    return CCTRUE;
}

case MK_SEM_EXPR_UNARY:
{
    const MKASTNode *node = sem_node(module, frame->id);
    if (!node || node->kind != MK_AST_UNARY)
        return sem_error(module, frame->id, "内部错误：期望一元表达式");
    MKASTNodeId operand = sem_child(module, frame->id, 0);
    if (node->as.unary.op == MK_UNARY_NOT)
    {
        MKType bool_type = mk_type_scalar(MK_VALUE_BOOL);
        if (!sem_type_expect(
                module, operand, input, &bool_type, "逻辑非"
            ))
            return CCFALSE;
        *out = bool_type;
        *done = CCTRUE;
        return CCTRUE;
    }
    if (!mk_type_is_numeric(input))
        return sem_error(
            module, frame->id, "一元 + / - 只能作用于数值"
        );
    if (frame->has_expected &&
        mk_type_is_numeric(&frame->expected) &&
        !sem_type_expect(
            module, frame->id, input, &frame->expected, "一元运算"
        ))
        return CCFALSE;
    *out = *input;
    *done = CCTRUE;
    return CCTRUE;
}

case MK_SEM_EXPR_BINARY:
{
    if (frame->stage == MK_SEM_EXPR_BINARY_LEFT)
    {
        frame->left_type = *input;
        frame->stage = MK_SEM_EXPR_BINARY_RIGHT;
        const MKType *expected =
            frame->child_has_expected
                ? &frame->child_expected : NULL;
        if (!sem_expr_push(
                module, frames, sem_child(module, frame->id, 1),
                frame->scope, expected
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
    if (!sem_expr_apply_binary(
            module, frame, &frame->left_type, input, out
        ))
        return CCFALSE;
    *done = CCTRUE;
    return CCTRUE;
}

case MK_SEM_EXPR_CALL:
{
    MKASTNodeId arg = sem_child(module, frame->id, frame->index);
    if (frame->function)
    {
        if (!sem_type_expect(
                module, arg, input, &frame->function->params[frame->index],
                "函数实参"
            ))
            return CCFALSE;
    }
    else
    {
        const MKType *expected = NULL;
        MKType formal;
        if (frame->index < frame->formal_count)
        {
            formal = frame->builtin->args[frame->index].type;
            expected = &formal;
        }
        if (expected && !mk_builtin_arg_matches(expected, input))
        {
            char actual_buf[64];
            char expected_buf[64];
            sem_type_format(input, actual_buf, sizeof(actual_buf));
            sem_type_format(expected, expected_buf, sizeof(expected_buf));
            return sem_error(
                module, arg,
                "内建实参类型错误：需要 %s，实际 %s（%s）",
                expected_buf, actual_buf, frame->builtin->signature
            );
        }
    }
    frame->index++;
    if (frame->index >= frame->count)
    {
        if (frame->function)
        {
            *out = frame->function->return_type;
            module->nodes[frame->id].function = frame->function;
        }
        else
        {
            *out = frame->builtin->return_type;
            module->nodes[frame->id].builtin = frame->builtin;
            if (frame->builtin->flags & MK_BUILTIN_NORETURN)
                module->nodes[frame->id].noreturn = CCTRUE;
        }
        *done = CCTRUE;
        return CCTRUE;
    }
    if (!sem_expr_call_next(module, frame, frames))
        return CCFALSE;
    *done = CCFALSE;
    return CCTRUE;
}

case MK_SEM_EXPR_INDEX:
{
    if (frame->stage == MK_SEM_EXPR_INDEX_BASE)
    {
        frame->left_type = *input;
        frame->stage = MK_SEM_EXPR_INDEX_INDEX;
        MKType int_type = mk_type_scalar(MK_VALUE_INT);
        if (!sem_expr_push(
                module, frames, sem_child(module, frame->id, 1),
                frame->scope, &int_type
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }
    if (frame->left_type.kind != MK_VALUE_LIST)
        return sem_error(module, frame->id, "索引只支持 list");
    if (frame->left_type.element == MK_VALUE_VOID)
        return sem_error(
            module, frame->id, "空列表无法确定索引元素类型"
        );
    if (input->kind != MK_VALUE_INT)
        return sem_error(module, frame->id, "list 索引必须是 int");
    *out = mk_type_scalar(frame->left_type.element);
    *done = CCTRUE;
    return CCTRUE;
}

default:
    return CCFALSE;
    }
}

static CCBOOL sem_expr_step(
    MKSemanticModule *module, MKSemExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, const MKType *input,
    MKType *out, CCBOOL *done
)
{
    if (!has_input)
        return sem_expr_step_start(module, frame, frames, input, out, done);
    return sem_expr_step_resume(module, frame, frames, input, out, done);
}

static CCBOOL sem_expr_run(
    MKSemanticModule *module, MKASTNodeId id,
    MKScope *scope, const MKType *expected,
    MKType *out
)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CCBOOL has_completed = CCFALSE;
    MKType completed;

    for (;;)
    {
        if (has_completed)
        {
            MKSemExprFrame *frame = sem_expr_top(&frames);
            if (!frame)
            {
                *out = completed;
                ccalloc(frames.array, 0, NULL);
                return CCTRUE;
            }
            MKType result;
            CCBOOL done = CCFALSE;
            if (!sem_expr_step(
                    module, frame, &frames, CCTRUE, &completed,
                    &result, &done
                ))
                goto Failed;
            if (done)
            {
                sem_set_type(module, frame->id, &result);
                MKSemExprFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!sem_expr_pop(&frames, &popped))
                    goto Failed;
                completed = result;
            }
            else
            {
                has_completed = CCFALSE;
            }
            continue;
        }

        if (frames.size == 0)
        {
            if (!sem_expr_push(
                    module, &frames, id, scope, expected
                ))
                goto Failed;
            continue;
        }

        MKSemExprFrame *frame = sem_expr_top(&frames);
        if (!frame)
            goto Failed;
        MKType result;
        CCBOOL done = CCFALSE;
        if (!sem_expr_step(
                module, frame, &frames, CCFALSE, NULL,
                &result, &done
            ))
            goto Failed;
        if (done)
        {
            sem_set_type(module, frame->id, &result);
            MKSemExprFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!sem_expr_pop(&frames, &popped))
                goto Failed;
            completed = result;
            has_completed = CCTRUE;
        }
    }

Failed:
    sem_expr_frames_abort(&frames);
    return CCFALSE;
}


static CCBOOL sem_expr(
    MKSemanticModule *module, MKASTNodeId id,
    MKScope *scope, const MKType *expected,
    MKType *out
)
{
    if (!module || !out)
        return CCFALSE;
    return sem_expr_run(
        module, id, scope, expected, out
    );
}

/* ============================================================================
 * 语句与控制流
 * ========================================================================== */

static CCBOOL sem_block(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    CCBOOL create_scope, const MKFunctionInfo *function,
    CCUINT32 loop_depth, MKSemFlow *flow
);

static CCBOOL sem_stmt_tree(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    CCBOOL create_scope, const MKFunctionInfo *function,
    CCUINT32 loop_depth, MKSemFlow *flow
);

static CCBOOL sem_declare_local(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    MKType *out_type
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node || node->kind != MK_AST_VAR_DECL)
        return sem_error(module, id, "内部错误：期望变量声明");

    MKType type;
    if (!sem_type_from_ast(
            module, sem_child(module, id, 0), &type
        ))
        return CCFALSE;
    if (type.kind == MK_VALUE_VOID)
        return sem_error(module, id, "变量类型不能是 void");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.var_decl.name, &name, &name_len))
        return sem_error(module, id, "变量名无效");

    MKASTNodeId init = sem_child(module, id, 1);
    MKType init_type;
    if (!sem_expr(
            module, init, scope, &type, &init_type
        ))
        return CCFALSE;
    if (!sem_type_expect(module, init, &init_type, &type, "变量初始化"))
        return CCFALSE;

    MKScopeAddResult added = scope_add(
        scope, name, name_len, MK_SYMBOL_VAR, type,
        node->as.var_decl.is_const, NULL
    );
    if (added != MK_SCOPE_ADD_OK)
    {
        return sem_error(
            module, id,
            added == MK_SCOPE_ADD_DUPLICATE
                ? "同一作用域内重复声明变量：%.*s"
                : "内存分配失败",
            (int)name_len, name
        );
    }
    if (out_type)
        *out_type = type;
    return CCTRUE;
}

static CCBOOL sem_stmt_assign(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node || node->kind != MK_AST_ASSIGN)
        return sem_error(module, id, "内部错误：期望赋值语句");

    MKASTNodeId lhs_id = sem_child(module, id, 0);
    MKASTNodeId value_id = sem_child(module, id, 1);
    const MKASTNode *lhs = sem_node(module, lhs_id);
    if (!lhs)
        return sem_error(module, id, "赋值目标缺失");

    if (lhs->kind == MK_AST_DISCARD)
    {
        if (node->as.assign.op != MK_ASSIGN_SET)
            return sem_error(module, id, "_ 不能使用 += ");
        MKType value_type;
        if (!sem_expr(
                module, value_id, scope, NULL, &value_type
            ))
            return CCFALSE;
        if (value_type.kind == MK_VALUE_VOID)
            return sem_error(module, value_id, "void 值不能被赋值或丢弃");
        return CCTRUE;
    }
    if (lhs->kind != MK_AST_IDENT)
        return sem_error(module, id, "赋值目标必须是变量或 _");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, lhs->as.ident.name, &name, &name_len))
        return sem_error(module, lhs_id, "变量名无效");
    const MKSymbol *symbol = scope_find(scope, name, name_len);
    if (!symbol || symbol->kind != MK_SYMBOL_VAR)
        return sem_error(
            module, lhs_id, "未定义的变量：%.*s",
            (int)name_len, name
        );
    if (symbol->is_const)
        return sem_error(
            module, lhs_id, "const 变量不能重新赋值：%.*s",
            (int)name_len, name
        );

    MKType value_type;
    if (node->as.assign.op == MK_ASSIGN_SET)
    {
        if (!sem_expr(
                module, value_id, scope, &symbol->type, &value_type
            ))
            return CCFALSE;
        return sem_type_expect(
            module, value_id, &value_type, &symbol->type, "赋值"
        );
    }

    if (symbol->type.kind == MK_VALUE_LIST)
    {
        MKType element = mk_type_scalar(symbol->type.element);
        if (!sem_expr(
                module, value_id, scope, &element, &value_type
            ))
            return CCFALSE;
        return sem_type_expect(
            module, value_id, &value_type, &element, "list 追加"
        );
    }
    if (!mk_type_is_numeric(&symbol->type))
        return sem_error(module, id, "+= 只支持数值和 list");
    if (!sem_expr(
            module, value_id, scope, &symbol->type, &value_type
        ))
        return CCFALSE;
    return sem_type_expect(
        module, value_id, &value_type, &symbol->type, "+= 运算"
    );
}

static CCBOOL sem_stmt_expr_stmt(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    MKSemFlow *flow
)
{
    MKASTNodeId expr = sem_child(module, id, 0);
    MKType type;
    if (!sem_expr(module, expr, scope, NULL, &type))
        return CCFALSE;
    if (type.kind != MK_VALUE_VOID &&
        !(module->nodes && expr < module->node_count &&
          module->nodes[expr].noreturn))
        return sem_error(module, expr, "表达式返回值未处理；请赋值或使用 _ 丢弃");
    if (module->nodes && expr < module->node_count &&
        module->nodes[expr].noreturn)
    {
        flow->reachable = CCFALSE;
        flow->returns = CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL sem_stmt_return(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    const MKFunctionInfo *function, MKSemFlow *flow
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node || node->kind != MK_AST_RETURN)
        return sem_error(module, id, "内部错误：期望 return");

    if (!function)
        return sem_error(module, id, "return 只能出现在函数内");

    if (node->as.return_stmt.has_value)
    {
        if (function->return_type.kind == MK_VALUE_VOID)
            return sem_error(module, id, "void 函数不能返回表达式");
        MKASTNodeId value = sem_child(module, id, 0);
        MKType value_type;
        if (!sem_expr(
                module, value, scope, &function->return_type, &value_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, value, &value_type,
                &function->return_type, "return"
            ))
            return CCFALSE;
    }
    else if (function->return_type.kind != MK_VALUE_VOID)
    {
        return sem_error(module, id, "非 void 函数必须返回值");
    }

    flow->reachable = CCFALSE;
    flow->returns = CCTRUE;
    return CCTRUE;
}


typedef enum mk_sem_frame_kind {
    MK_SEM_FRAME_BLOCK = 0,
    MK_SEM_FRAME_IF,
    MK_SEM_FRAME_WHILE,
    MK_SEM_FRAME_FOR
} MKSemFrameKind;

typedef enum mk_sem_frame_stage {
    MK_SEM_STAGE_ITEMS = 0,
    MK_SEM_STAGE_THEN,
    MK_SEM_STAGE_ELSE,
    MK_SEM_STAGE_BODY
} MKSemFrameStage;

typedef struct mk_sem_frame {
    MKSemFrameKind kind;
    MKSemFrameStage stage;
    MKASTNodeId id;
    MKScope *scope;
    CCBOOL owns_scope;
    const MKFunctionInfo *function;
    CCUINT32 loop_depth;
    CCUINT32 index;
    CCUINT32 count;
    MKSemFlow flow;
    MKSemFlow branch;
    CCBOOL has_else;
} MKSemFrame;

static CCBOOL sem_stmt_simple(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    const MKFunctionInfo *function, CCUINT32 loop_depth,
    MKSemFlow *flow
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node)
        return sem_error(module, id, "内部错误：语句节点缺失");

    flow->reachable = CCTRUE;
    flow->returns = CCFALSE;
    switch (node->kind)
    {
    case MK_AST_VAR_DECL:
        return sem_declare_local(module, id, scope, NULL);

    case MK_AST_ASSIGN:
        return sem_stmt_assign(module, id, scope);

    case MK_AST_EXPR_STMT:
        return sem_stmt_expr_stmt(module, id, scope, flow);

    case MK_AST_RETURN:
        return sem_stmt_return(module, id, scope, function, flow);

    case MK_AST_BREAK:
    case MK_AST_CONTINUE:
        if (loop_depth == 0)
            return sem_error(module, id, "break/continue 只能出现在循环内");
        flow->reachable = CCFALSE;
        flow->returns = CCFALSE;
        return CCTRUE;

    default:
        return sem_error(
            module, id, "内部错误：不是简单语句：%s",
            mk_ast_kind_name(node->kind)
        );
    }
}

static CCBOOL sem_frame_begin(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    CCBOOL create_scope, const MKFunctionInfo *function,
    CCUINT32 loop_depth, MKSemFrame *frame,
    CCBOOL *is_simple, MKSemFlow *simple_flow
)
{
    memset(frame, 0, sizeof(*frame));
    *is_simple = CCFALSE;

    const MKASTNode *node = sem_node(module, id);
    if (!node)
        return sem_error(module, id, "内部错误：语句节点缺失");

    frame->id = id;
    frame->scope = scope;
    frame->function = function;
    frame->loop_depth = loop_depth;
    frame->flow.reachable = CCTRUE;
    frame->flow.returns = CCFALSE;

    switch (node->kind)
    {
    case MK_AST_BLOCK:
        frame->kind = MK_SEM_FRAME_BLOCK;
        frame->stage = MK_SEM_STAGE_ITEMS;
        if (create_scope)
        {
            MKScope *block_scope = scope_create(scope);
            if (!block_scope)
                return sem_error(module, id, "内存分配失败");
            frame->scope = block_scope;
            frame->owns_scope = CCTRUE;
        }
        frame->count = sem_child_count(module, id);
        return CCTRUE;

    case MK_AST_IF:
    {
        MKType bool_type = mk_type_scalar(MK_VALUE_BOOL);
        MKType condition_type;
        if (!sem_expr(
                module, sem_child(module, id, 0), scope,
                &bool_type, &condition_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, sem_child(module, id, 0), &condition_type,
                &bool_type, "if 条件"
            ))
            return CCFALSE;
        frame->kind = MK_SEM_FRAME_IF;
        frame->stage = MK_SEM_STAGE_THEN;
        frame->has_else = node->as.if_stmt.has_else;
        return CCTRUE;
    }

    case MK_AST_WHILE:
    {
        MKType bool_type = mk_type_scalar(MK_VALUE_BOOL);
        MKType condition_type;
        if (!sem_expr(
                module, sem_child(module, id, 0), scope,
                &bool_type, &condition_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, sem_child(module, id, 0), &condition_type,
                &bool_type, "while 条件"
            ))
            return CCFALSE;
        frame->kind = MK_SEM_FRAME_WHILE;
        frame->stage = MK_SEM_STAGE_BODY;
        return CCTRUE;
    }

    case MK_AST_FOR_IN:
    {
        MKType element;
        if (!sem_type_from_ast(
                module, sem_child(module, id, 0), &element
            ))
            return CCFALSE;
        if (element.kind == MK_VALUE_VOID)
            return sem_error(module, id, "for-in 元素类型不能是 void");

        MKType list_type = mk_type_list(element.kind);
        MKType iterable_type;
        if (!sem_expr(
                module, sem_child(module, id, 1), scope,
                &list_type, &iterable_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, sem_child(module, id, 1), &iterable_type,
                &list_type, "for-in 可迭代对象"
            ))
            return CCFALSE;

        MKScope *loop_scope = scope_create(scope);
        if (!loop_scope)
            return sem_error(module, id, "内存分配失败");

        const char *name = NULL;
        CCUINT32 name_len = 0;
        if (!sem_string(module, node->as.for_in.name, &name, &name_len))
        {
            scope_destroy(loop_scope);
            return sem_error(module, id, "for-in 变量名无效");
        }
        MKScopeAddResult added = scope_add(
            loop_scope, name, name_len, MK_SYMBOL_VAR,
            element, CCFALSE, NULL
        );
        if (added != MK_SCOPE_ADD_OK)
        {
            scope_destroy(loop_scope);
            return sem_error(module, id, "for-in 变量声明失败");
        }

        frame->kind = MK_SEM_FRAME_FOR;
        frame->stage = MK_SEM_STAGE_BODY;
        frame->scope = loop_scope;
        frame->owns_scope = CCTRUE;
        frame->loop_depth = loop_depth + 1;
        return CCTRUE;
    }

    default:
        *is_simple = CCTRUE;
        return sem_stmt_simple(
            module, id, scope, function, loop_depth, simple_flow
        );
    }
}

static MKSemFrame *sem_frame_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(MKSemFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKSemFrame);
    return &((MKSemFrame *)frames->array)[count - 1];
}

static CCBOOL sem_frame_push(
    MKSemanticModule *module, CCDyn *frames, const MKSemFrame *frame
)
{
    if (!mk_dyn_push(
            frames, (CCUINT32)sizeof(MKSemFrame),
            (CCINT32)sizeof(MKSemFrame), frame
        ))
        return sem_error(module, frame->id, "内存分配失败");
    return CCTRUE;
}

static CCBOOL sem_frame_pop(CCDyn *frames, MKSemFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(MKSemFrame),
        (CCINT32)sizeof(MKSemFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void sem_frame_scope_release(MKSemFrame *frame)
{
    if (frame->owns_scope && frame->scope)
    {
        scope_destroy(frame->scope);
        frame->scope = NULL;
        frame->owns_scope = CCFALSE;
    }
}

static void sem_frames_abort(CCDyn *frames)
{
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKSemFrame);
    for (CCUINT32 i = count; i > 0; i--)
        sem_frame_scope_release(
            &((MKSemFrame *)frames->array)[i - 1]
        );
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL sem_stmt_tree(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    CCBOOL create_scope, const MKFunctionInfo *function,
    CCUINT32 loop_depth, MKSemFlow *flow
)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CCBOOL has_completed = CCFALSE;
    MKSemFlow completed;
    memset(&completed, 0, sizeof(completed));

    for (;;)
    {
        if (has_completed)
        {
            MKSemFrame *frame = sem_frame_top(&frames);
            if (!frame)
            {
                *flow = completed;
                ccalloc(frames.array, 0, NULL);
                return CCTRUE;
            }

            if (frame->kind == MK_SEM_FRAME_BLOCK)
            {
                if (!completed.reachable)
                {
                    frame->flow.reachable = CCFALSE;
                    frame->flow.returns = completed.returns;
                }
                has_completed = CCFALSE;
                continue;
            }

            if (frame->kind == MK_SEM_FRAME_IF)
            {
                if (frame->stage == MK_SEM_STAGE_THEN)
                {
                    frame->branch = completed;
                    if (frame->has_else)
                    {
                        frame->stage = MK_SEM_STAGE_ELSE;
                        has_completed = CCFALSE;
                        continue;
                    }
                    completed.reachable = CCTRUE;
                    completed.returns = CCFALSE;
                }
                else
                {
                    completed.reachable =
                        frame->branch.reachable || completed.reachable;
                    completed.returns =
                        frame->branch.returns && completed.returns;
                }
            }
            else
            {
                completed.reachable = CCTRUE;
                completed.returns = CCFALSE;
            }

            sem_frame_scope_release(frame);
            MKSemFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!sem_frame_pop(&frames, &popped))
                goto Failed;
            has_completed = CCTRUE;
            continue;
        }

        if (frames.size == 0)
        {
            MKSemFrame start;
            CCBOOL is_simple = CCFALSE;
            if (!sem_frame_begin(
                    module, id, scope, create_scope, function,
                    loop_depth, &start, &is_simple, &completed
                ))
                goto Failed;
            if (is_simple)
            {
                *flow = completed;
                ccalloc(frames.array, 0, NULL);
                return CCTRUE;
            }
            if (!sem_frame_push(module, &frames, &start))
            {
                sem_frame_scope_release(&start);
                goto Failed;
            }
            continue;
        }

        MKSemFrame *frame = sem_frame_top(&frames);
        if (!frame)
            goto Failed;

        MKASTNodeId child_id = MK_AST_NODE_NIL;
        MKScope *child_scope = frame->scope;
        CCUINT32 child_loop_depth = frame->loop_depth;
        CCBOOL child_create_scope = CCTRUE;

        if (frame->kind == MK_SEM_FRAME_BLOCK)
        {
            if (frame->index >= frame->count)
            {
                completed = frame->flow;
                sem_frame_scope_release(frame);
                MKSemFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!sem_frame_pop(&frames, &popped))
                    goto Failed;
                has_completed = CCTRUE;
                continue;
            }
            child_id = sem_child(module, frame->id, frame->index);
            frame->index++;
        }
        else if (frame->kind == MK_SEM_FRAME_IF)
        {
            child_id = sem_child(
                module, frame->id,
                frame->stage == MK_SEM_STAGE_THEN ? 1 : 2
            );
        }
        else if (frame->kind == MK_SEM_FRAME_WHILE)
        {
            child_id = sem_child(module, frame->id, 1);
            child_loop_depth = frame->loop_depth + 1;
        }
        else
        {
            child_id = sem_child(module, frame->id, 2);
        }

        MKSemFrame child;
        CCBOOL is_simple = CCFALSE;
        MKSemFlow simple_flow;
        if (!sem_frame_begin(
                module, child_id, child_scope, child_create_scope,
                function, child_loop_depth,
                &child, &is_simple, &simple_flow
            ))
            goto Failed;
        if (is_simple)
        {
            completed = simple_flow;
            has_completed = CCTRUE;
            continue;
        }
        if (!sem_frame_push(module, &frames, &child))
        {
            sem_frame_scope_release(&child);
            goto Failed;
        }
    }

Failed:
    sem_frames_abort(&frames);
    return CCFALSE;
}

static CCBOOL sem_block(
    MKSemanticModule *module, MKASTNodeId id, MKScope *scope,
    CCBOOL create_scope, const MKFunctionInfo *function,
    CCUINT32 loop_depth, MKSemFlow *flow
)
{
    const MKASTNode *node = sem_node(module, id);
    if (!node || node->kind != MK_AST_BLOCK)
        return sem_error(module, id, "内部错误：期望代码块");
    return sem_stmt_tree(
        module, id, scope, create_scope, function, loop_depth, flow
    );
}

/* ============================================================================
 * 函数与模块
 * ========================================================================== */

static CCBOOL sem_analyze_function(
    MKSemanticModule *module, MKFunctionInfo *function
)
{
    MKScope *function_scope = scope_create(module->module_scope);
    if (!function_scope)
        return sem_error(module, function->decl, "内存分配失败");

    const MKASTNode *decl = sem_node(module, function->decl);
    if (!decl)
    {
        scope_destroy(function_scope);
        return sem_error(module, function->decl, "函数声明缺失");
    }

    for (CCUINT32 i = 0; i < function->param_count; i++)
    {
        MKASTNodeId param = sem_child(module, function->decl, i + 1);
        const MKASTNode *param_node = sem_node(module, param);
        if (!param_node)
        {
            scope_destroy(function_scope);
            return sem_error(module, param, "函数参数缺失");
        }
        const char *name = NULL;
        CCUINT32 name_len = 0;
        if (!sem_string(
                module, param_node->as.param.name, &name, &name_len
            ))
        {
            scope_destroy(function_scope);
            return sem_error(module, param, "参数名无效");
        }
        MKScopeAddResult added = scope_add(
            function_scope, name, name_len, MK_SYMBOL_VAR,
            function->params[i], CCFALSE, NULL
        );
        if (added != MK_SCOPE_ADD_OK)
        {
            scope_destroy(function_scope);
            return sem_error(module, param, "重复的函数参数：%s", name);
        }
    }

    MKASTNodeId body = sem_child(
        module, function->decl, function->param_count + 1
    );
    const MKASTNode *body_node = sem_node(module, body);
    if (!body_node || body_node->kind != MK_AST_BLOCK)
    {
        scope_destroy(function_scope);
        return sem_error(module, body, "函数体缺失");
    }

    MKSemFlow flow;
    CCBOOL ok = sem_block(
        module, body, function_scope, CCFALSE,
        function, 0, &flow
    );
    scope_destroy(function_scope);
    if (!ok)
        return CCFALSE;

    if (function->return_type.kind != MK_VALUE_VOID && flow.reachable)
        return sem_error(
            module, function->decl,
            "非 void 函数 %s 缺少返回值",
            function->name.data
        );
    return CCTRUE;
}

static CCBOOL sem_analyze_global(
    MKSemanticModule *module, MKASTNodeId decl
)
{
    const MKASTNode *node = sem_node(module, decl);
    if (!node)
        return sem_error(module, decl, "变量声明缺失");
    MKType type;
    if (!sem_type_from_ast(
            module, sem_child(module, decl, 0), &type
        ))
        return CCFALSE;
    MKType value_type;
    if (!sem_expr(
            module, sem_child(module, decl, 1), module->module_scope,
            &type, &value_type
        ))
        return CCFALSE;
    return sem_type_expect(
        module, sem_child(module, decl, 1),
        &value_type, &type, "变量初始化"
    );
}

static CCBOOL sem_add_imported_functions(MKSemanticModule *module)
{
    MKImportedFunction **imports =
        (MKImportedFunction **)module->imported_functions.array;
    CCUINT32 count = module->imported_functions.size /
        (CCUINT32)sizeof(MKImportedFunction *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const MKImportedFunction *import = imports[i];
        if (!import || !import->function)
            return sem_error(
                module, mk_ast_root(module->ast),
                "导入函数记录损坏"
            );
        if (mk_builtin_find(
                module->builtins, import->name.data, import->name.len
            ))
            return sem_error(
                module, mk_ast_root(module->ast),
                "导入函数不能与内建同名：%s", import->name.data
            );

        MKScopeAddResult added = scope_add(
            module->module_scope,
            import->name.data, import->name.len,
            MK_SYMBOL_FUNCTION, import->function->return_type,
            CCFALSE, import->function
        );
        if (added != MK_SCOPE_ADD_OK)
        {
            return sem_error(
                module, mk_ast_root(module->ast),
                added == MK_SCOPE_ADD_DUPLICATE
                    ? "导入函数与本地符号重名：%s"
                    : "内存分配失败",
                import->name.data
            );
        }
    }
    return CCTRUE;
}

CCBOOL mk_semantic_analyze(MKSemanticModule *module)
{
    if (!module || !module->ast)
        return CCFALSE;
    if (module->analyzed)
        return module->ok;

    module->ok = CCTRUE;
    module->module_scope = scope_create(NULL);
    if (!module->module_scope)
    {
        module->ok = CCFALSE;
        return CCFALSE;
    }

    MKASTNodeId root = mk_ast_root(module->ast);
    CCUINT32 root_count = sem_child_count(module, root);

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        MKASTNodeId item = sem_child(module, root, i);
        const MKASTNode *node = sem_node(module, item);
        if (node && node->kind == MK_AST_FUNC_DECL)
        {
            if (!sem_predeclare_function(module, item))
                goto Failed;
        }
    }

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        MKASTNodeId item = sem_child(module, root, i);
        const MKASTNode *node = sem_node(module, item);
        if (node && node->kind == MK_AST_VAR_DECL)
        {
            if (!sem_predeclare_global(module, item))
                goto Failed;
        }
    }

    if (!sem_add_imported_functions(module))
        goto Failed;

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        MKASTNodeId item = sem_child(module, root, i);
        const MKASTNode *node = sem_node(module, item);
        if (node && node->kind == MK_AST_VAR_DECL)
        {
            if (!sem_analyze_global(module, item))
                goto Failed;
        }
    }

    MKFunctionInfo **functions =
        (MKFunctionInfo **)module->functions.array;
    CCUINT32 function_count =
        module->functions.size / (CCUINT32)sizeof(MKFunctionInfo *);
    for (CCUINT32 i = 0; i < function_count; i++)
    {
        if (!sem_analyze_function(module, functions[i]))
            goto Failed;
    }

    module->analyzed = CCTRUE;
    module->ok = CCTRUE;
    return CCTRUE;

Failed:
    module->analyzed = CCTRUE;
    module->ok = CCFALSE;
    return CCFALSE;
}

const MKType *mk_semantic_node_type(
    const MKSemanticModule *module, MKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return NULL;
    return &module->nodes[node].type;
}

const MKFunctionInfo *mk_semantic_node_function(
    const MKSemanticModule *module, MKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return NULL;
    return module->nodes[node].function;
}

const MKBuiltinSpec *mk_semantic_node_builtin(
    const MKSemanticModule *module, MKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return NULL;
    return module->nodes[node].builtin;
}

CCBOOL mk_semantic_node_is_noreturn(
    const MKSemanticModule *module, MKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return CCFALSE;
    return module->nodes[node].noreturn;
}

const MKFunctionInfo *mk_semantic_find_function(
    const MKSemanticModule *module, const char *name, CCUINT32 len
)
{
    if (!module || !name)
        return NULL;
    MKFunctionInfo **functions =
        (MKFunctionInfo **)module->functions.array;
    CCUINT32 count =
        module->functions.size / (CCUINT32)sizeof(MKFunctionInfo *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (functions[i]->name.len == len &&
            memcmp(functions[i]->name.data, name, len) == 0)
            return functions[i];
    }
    return NULL;
}


CCUINT32 mk_semantic_function_count(
    const MKSemanticModule *module
)
{
    if (!module)
        return 0;
    return module->functions.size /
        (CCUINT32)sizeof(MKFunctionInfo *);
}

const MKFunctionInfo *mk_semantic_function_at(
    const MKSemanticModule *module, CCUINT32 index
)
{
    if (!module || index >= mk_semantic_function_count(module))
        return NULL;
    MKFunctionInfo **functions =
        (MKFunctionInfo **)module->functions.array;
    return functions[index];
}
