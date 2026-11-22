/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 语义分析：作用域、类型、函数、内建、target 与配置引用
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_semantic.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdarg.h>
#include <string.h>

#define CMK_SEM_COMMENT CC_DBG_REL_TAG(".cmk 语义", NULL)

typedef enum cmk_scope_add_result {
    CMK_SCOPE_ADD_OK = 0,
    CMK_SCOPE_ADD_DUPLICATE,
    CMK_SCOPE_ADD_NOMEM
} CMKScopeAddResult;

typedef struct cmk_sem_flow {
    CCBOOL reachable;
    CCBOOL returns;
} CMKSemFlow;

/* ============================================================================
 * AST 访问与诊断
 * ========================================================================== */

static const CMKASTNode *sem_node(
    const CMKSemanticModule *module, CMKASTNodeId id
)
{
    if (!module || !module->ast)
        return NULL;
    return cmk_ast_node(module->ast, id);
}

static CMKASTNodeId sem_child(
    const CMKSemanticModule *module, CMKASTNodeId parent, CCUINT32 index
)
{
    const CMKASTNode *node = sem_node(module, parent);
    if (!node)
        return CMK_AST_NODE_NIL;
    CMKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < index && child != CMK_AST_NODE_NIL; i++)
    {
        const CMKASTNode *child_node = sem_node(module, child);
        if (!child_node)
            return CMK_AST_NODE_NIL;
        child = child_node->next_sibling;
    }
    return child;
}

static CCUINT32 sem_child_count(
    const CMKSemanticModule *module, CMKASTNodeId parent
)
{
    const CMKASTNode *node = sem_node(module, parent);
    if (!node)
        return 0;
    CCUINT32 count = 0;
    CMKASTNodeId child = node->first_child;
    while (child != CMK_AST_NODE_NIL)
    {
        count++;
        const CMKASTNode *child_node = sem_node(module, child);
        if (!child_node)
            break;
        child = child_node->next_sibling;
    }
    return count;
}

static CCBOOL sem_string(
    const CMKSemanticModule *module, CMKASTStringId id,
    const char **out, CCUINT32 *out_len
)
{
    if (!module || !module->ast)
        return CCFALSE;
    CCUINT32 len = 0;
    const char *text = cmk_ast_string(module->ast, id, &len);
    if (!text)
        return CCFALSE;
    if (out)
        *out = text;
    if (out_len)
        *out_len = len;
    return CCTRUE;
}

static CCBOOL sem_error(
    CMKSemanticModule *module, CMKASTNodeId node,
    const char *fmt, ...
)
{
    if (!module || !module->diagnostic)
        return CCFALSE;
    if (cmk_diagnostic_has(module->diagnostic))
        return CCFALSE;

    const CMKASTNode *ast_node = sem_node(module, node);
    const char *file = module->path;
    CCUINT32 line = 0;
    CCUINT32 col = 0;
    if (ast_node)
    {
        file = cmk_ast_file(module->ast);
        line = ast_node->span.line;
        col = ast_node->span.col;
    }
    if (!file)
        file = "(.cmk)";

    va_list ap;
    va_start(ap, fmt);
    (void)cmk_diagnostic_vset(
        module->diagnostic, CMK_DIAG_ERROR, file, line, col, fmt, ap
    );
    va_end(ap);
    module->ok = CCFALSE;
    return CCFALSE;
}

static CCBOOL sem_type_format(
    const CMKType *type, char *buf, CCUINT32 size
)
{
    return cmk_type_format(type, buf, size);
}

/* ============================================================================
 * 作用域
 * ========================================================================== */

static CMKScope *scope_create(CMKScope *parent)
{
    CMKScope *scope = ccalloc(NULL, sizeof(CMKScope), CMK_SEM_COMMENT);
    if (!scope)
        return NULL;
    memset(scope, 0, sizeof(*scope));
    scope->parent = parent;
    return scope;
}

static void scope_destroy(CMKScope *scope)
{
    if (!scope)
        return;
    CMKSymbol *symbols = (CMKSymbol *)scope->symbols.array;
    CCUINT32 count =
        scope->symbols.size / (CCUINT32)sizeof(CMKSymbol);
    for (CCUINT32 i = 0; i < count; i++)
        cmk_string_clear(&symbols[i].name);
    if (scope->symbols.array)
        ccalloc(scope->symbols.array, 0, NULL);
    ccalloc(scope, 0, NULL);
}

static const CMKSymbol *scope_find(
    const CMKScope *scope, const char *name, CCUINT32 len
)
{
    for (const CMKScope *current = scope;
         current;
         current = current->parent)
    {
        const CMKSymbol *symbols = (const CMKSymbol *)current->symbols.array;
        CCUINT32 count =
            current->symbols.size / (CCUINT32)sizeof(CMKSymbol);
        for (CCUINT32 i = 0; i < count; i++)
        {
            const CMKSymbol *symbol = &symbols[i];
            if (symbol->name.len == len &&
                (len == 0 ||
                 memcmp(symbol->name.data, name, len) == 0))
                return symbol;
        }
    }
    return NULL;
}

static CMKScopeAddResult scope_add(
    CMKScope *scope, const char *name, CCUINT32 len,
    CMKSymbolKind kind, CMKType type, CCBOOL is_const,
    const CMKFunctionInfo *function, const CMKTargetInfo *target
)
{
    if (!scope || !name)
        return CMK_SCOPE_ADD_NOMEM;

    const CMKSymbol *symbols = (const CMKSymbol *)scope->symbols.array;
    CCUINT32 count = scope->symbols.size / (CCUINT32)sizeof(CMKSymbol);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (symbols[i].name.len == len &&
            (len == 0 || memcmp(symbols[i].name.data, name, len) == 0))
            return CMK_SCOPE_ADD_DUPLICATE;
    }

    CMKSymbol symbol;
    memset(&symbol, 0, sizeof(symbol));
    if (!cmk_string_set(&symbol.name, name, len))
        return CMK_SCOPE_ADD_NOMEM;
    symbol.kind = kind;
    symbol.type = type;
    symbol.is_const = is_const;
    symbol.function = function;
    symbol.target = target;

    if (!cmk_dyn_push(
            &scope->symbols, (CCUINT32)sizeof(CMKSymbol),
            (CCINT32)sizeof(CMKSymbol), &symbol
        ))
    {
        cmk_string_clear(&symbol.name);
        return CMK_SCOPE_ADD_NOMEM;
    }
    return CMK_SCOPE_ADD_OK;
}

/* ============================================================================
 * 类型
 * ========================================================================== */

static CCBOOL sem_type_from_ast(
    CMKSemanticModule *module, CMKASTNodeId type_node,
    CCBOOL allow_target_ref, CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, type_node);
    if (!node || node->kind != CMK_AST_TYPE)
        return sem_error(module, type_node, "内部错误：期望类型节点");

    switch (node->as.type.kind)
    {
    case CMK_TYPE_VOID:
        *out = cmk_type_scalar(CMK_VALUE_VOID);
        return CCTRUE;

    case CMK_TYPE_INT:
        *out = cmk_type_scalar(CMK_VALUE_INT);
        return CCTRUE;

    case CMK_TYPE_FLOAT:
        *out = cmk_type_scalar(CMK_VALUE_FLOAT);
        return CCTRUE;

    case CMK_TYPE_BOOL:
        *out = cmk_type_scalar(CMK_VALUE_BOOL);
        return CCTRUE;

    case CMK_TYPE_STRING:
        *out = cmk_type_scalar(CMK_VALUE_STRING);
        return CCTRUE;

    case CMK_TYPE_ACTION:
        *out = cmk_type_scalar(CMK_VALUE_ACTION);
        return CCTRUE;

    case CMK_TYPE_TARGET_REF:
        if (!allow_target_ref)
            return sem_error(
                module, type_node,
                "target_ref 只能用于 target 的 depends_on 字段"
            );
        *out = cmk_type_scalar(CMK_VALUE_TARGET_REF);
        return CCTRUE;

    case CMK_TYPE_LIST:
    {
        CMKASTNodeId element_id = sem_child(module, type_node, 0);
        CMKType element;
        if (!sem_type_from_ast(
                module, element_id, allow_target_ref, &element
            ))
            return CCFALSE;
        if (element.kind == CMK_VALUE_LIST)
            return sem_error(module, type_node, "不允许嵌套 list 类型");
        if (element.kind == CMK_VALUE_VOID)
            return sem_error(module, type_node, "list 元素类型不能是 void");
        *out = cmk_type_list(element.kind);
        return CCTRUE;
    }

    default:
        return sem_error(module, type_node, "未知的类型节点");
    }
}

static CCBOOL sem_type_expect(
    CMKSemanticModule *module, CMKASTNodeId node,
    const CMKType *actual, const CMKType *expected,
    const char *context
)
{
    if (cmk_type_equal(actual, expected))
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

CMKSemanticModule *cmk_semantic_module_create(
    CMKAST *ast, const char *path,
    CMKConfig *config, CMKBuiltinRegistry *builtins,
    CMKDiagnostic *diagnostic
)
{
    if (!ast || !path || !config || !builtins || !diagnostic)
        return NULL;

    CMKSemanticModule *module =
        ccalloc(NULL, sizeof(CMKSemanticModule), CMK_SEM_COMMENT);
    if (!module)
        return NULL;
    memset(module, 0, sizeof(*module));

    size_t path_len = strlen(path);
    module->path = ccalloc(
        NULL, (CCUINT64)path_len + 1, CMK_SEM_COMMENT
    );
    if (!module->path)
    {
        cmk_semantic_module_destroy(module);
        return NULL;
    }
    memcpy(module->path, path, path_len + 1);

    module->ast = ast;
    module->config = config;
    module->builtins = builtins;
    module->diagnostic = diagnostic;
    module->node_count = cmk_ast_node_count(ast);
    if (module->node_count > 0)
    {
        module->nodes = ccalloc(
            NULL,
            (CCUINT64)module->node_count * sizeof(CMKNodeSemantics),
            CMK_SEM_COMMENT
        );
        if (!module->nodes)
        {
            cmk_semantic_module_destroy(module);
            return NULL;
        }
        memset(
            module->nodes, 0,
            (size_t)module->node_count * sizeof(CMKNodeSemantics)
        );
    }
    return module;
}

static void function_info_destroy(CMKFunctionInfo *function)
{
    if (!function)
        return;
    cmk_string_clear(&function->name);
    if (function->params)
        ccalloc(function->params, 0, NULL);
    ccalloc(function, 0, NULL);
}

static void target_info_destroy(CMKTargetInfo *target)
{
    if (!target)
        return;
    cmk_string_clear(&target->name);
    ccalloc(target, 0, NULL);
}

void cmk_semantic_module_destroy(CMKSemanticModule *module)
{
    if (!module)
        return;

    CMKFunctionInfo **functions =
        (CMKFunctionInfo **)module->functions.array;
    CCUINT32 function_count =
        module->functions.size / (CCUINT32)sizeof(CMKFunctionInfo *);
    for (CCUINT32 i = 0; i < function_count; i++)
        function_info_destroy(functions[i]);
    if (module->functions.array)
        ccalloc(module->functions.array, 0, NULL);

    CMKTargetInfo **targets = (CMKTargetInfo **)module->targets.array;
    CCUINT32 target_count =
        module->targets.size / (CCUINT32)sizeof(CMKTargetInfo *);
    for (CCUINT32 i = 0; i < target_count; i++)
        target_info_destroy(targets[i]);
    if (module->targets.array)
        ccalloc(module->targets.array, 0, NULL);

    CMKImportedFunction **imported_functions =
        (CMKImportedFunction **)module->imported_functions.array;
    CCUINT32 imported_function_count =
        module->imported_functions.size /
        (CCUINT32)sizeof(CMKImportedFunction *);
    for (CCUINT32 i = 0; i < imported_function_count; i++)
    {
        CMKImportedFunction *record = imported_functions[i];
        if (!record)
            continue;
        cmk_string_clear(&record->name);
        ccalloc(record, 0, NULL);
    }
    if (module->imported_functions.array)
        ccalloc(module->imported_functions.array, 0, NULL);

    CMKImportedTarget **imported_targets =
        (CMKImportedTarget **)module->imported_targets.array;
    CCUINT32 imported_target_count =
        module->imported_targets.size /
        (CCUINT32)sizeof(CMKImportedTarget *);
    for (CCUINT32 i = 0; i < imported_target_count; i++)
    {
        CMKImportedTarget *record = imported_targets[i];
        if (!record)
            continue;
        cmk_string_clear(&record->alias);
        ccalloc(record, 0, NULL);
    }
    if (module->imported_targets.array)
        ccalloc(module->imported_targets.array, 0, NULL);

    if (module->module_scope)
        scope_destroy(module->module_scope);
    if (module->nodes)
        ccalloc(module->nodes, 0, NULL);
    if (module->path)
        ccalloc(module->path, 0, NULL);
    ccalloc(module, 0, NULL);
}

CCBOOL cmk_semantic_add_imported_function(
    CMKSemanticModule *module,
    const char *name, CCUINT32 name_len,
    const CMKFunctionInfo *function
)
{
    if (!module || !name || !function)
        return CCFALSE;
    CMKImportedFunction *record =
        ccalloc(NULL, sizeof(CMKImportedFunction), CMK_SEM_COMMENT);
    if (!record)
        return CCFALSE;
    memset(record, 0, sizeof(*record));
    if (!cmk_string_set(&record->name, name, name_len))
    {
        ccalloc(record, 0, NULL);
        return CCFALSE;
    }
    record->function = function;
    if (!cmk_dyn_push(
            &module->imported_functions,
            (CCUINT32)sizeof(CMKImportedFunction *),
            (CCINT32)sizeof(CMKImportedFunction *), &record
        ))
    {
        cmk_string_clear(&record->name);
        ccalloc(record, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL cmk_semantic_add_imported_target(
    CMKSemanticModule *module,
    const char *alias, CCUINT32 alias_len,
    const CMKTargetInfo *target
)
{
    if (!module || !alias || !target)
        return CCFALSE;
    CMKImportedTarget *record =
        ccalloc(NULL, sizeof(CMKImportedTarget), CMK_SEM_COMMENT);
    if (!record)
        return CCFALSE;
    memset(record, 0, sizeof(*record));
    if (!cmk_string_set(&record->alias, alias, alias_len))
    {
        ccalloc(record, 0, NULL);
        return CCFALSE;
    }
    record->target = target;
    if (!cmk_dyn_push(
            &module->imported_targets,
            (CCUINT32)sizeof(CMKImportedTarget *),
            (CCINT32)sizeof(CMKImportedTarget *), &record
        ))
    {
        cmk_string_clear(&record->alias);
        ccalloc(record, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

/* ============================================================================
 * 预声明
 * ========================================================================== */

static const CMKTargetInfo *sem_find_target_name(
    const CMKSemanticModule *module, const char *name, CCUINT32 len
)
{
    CMKTargetInfo **targets = (CMKTargetInfo **)module->targets.array;
    CCUINT32 count =
        module->targets.size / (CCUINT32)sizeof(CMKTargetInfo *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKTargetInfo *target = targets[i];
        if (target->name.len == len &&
            (len == 0 || memcmp(target->name.data, name, len) == 0))
            return target;
    }
    return NULL;
}

static CCBOOL sem_predeclare_function(
    CMKSemanticModule *module, CMKASTNodeId decl
)
{
    const CMKASTNode *node = sem_node(module, decl);
    if (!node || node->kind != CMK_AST_FUNC_DECL)
        return sem_error(module, decl, "内部错误：期望函数声明");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.func_decl.name, &name, &name_len))
        return sem_error(module, decl, "函数名无效");

    if (cmk_builtin_find(module->builtins, name, name_len))
        return sem_error(
            module, decl, "用户函数不能与内建函数同名：%.*s",
            (int)name_len, name
        );

    CCUINT32 child_count = sem_child_count(module, decl);
    if (node->as.func_decl.param_count + 2 != child_count)
        return sem_error(module, decl, "函数声明子节点数量错误");

    CMKFunctionInfo *function =
        ccalloc(NULL, sizeof(CMKFunctionInfo), CMK_SEM_COMMENT);
    if (!function)
        return sem_error(module, decl, "内存分配失败");
    memset(function, 0, sizeof(*function));
    function->decl = decl;
    function->module = module;
    if (!cmk_string_set(&function->name, name, name_len))
    {
        function_info_destroy(function);
        return sem_error(module, decl, "内存分配失败");
    }

    CMKASTNodeId return_type_id = sem_child(module, decl, 0);
    if (!sem_type_from_ast(
            module, return_type_id, CCFALSE, &function->return_type
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
            (CCUINT64)param_count * sizeof(CMKType),
            CMK_SEM_COMMENT
        );
        if (!function->params)
        {
            function_info_destroy(function);
            return sem_error(module, decl, "内存分配失败");
        }
        memset(
            function->params, 0,
            (size_t)param_count * sizeof(CMKType)
        );
    }
    function->param_count = param_count;

    for (CCUINT32 i = 0; i < param_count; i++)
    {
        CMKASTNodeId param = sem_child(module, decl, i + 1);
        const CMKASTNode *param_node = sem_node(module, param);
        if (!param_node || param_node->kind != CMK_AST_PARAM)
        {
            function_info_destroy(function);
            return sem_error(module, decl, "函数参数节点错误");
        }
        CMKASTNodeId param_type = sem_child(module, param, 0);
        if (!sem_type_from_ast(
                module, param_type, CCFALSE, &function->params[i]
            ))
        {
            function_info_destroy(function);
            return CCFALSE;
        }
        if (function->params[i].kind == CMK_VALUE_VOID)
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
            CMKASTNodeId previous = sem_child(module, decl, j + 1);
            const CMKASTNode *previous_node = sem_node(module, previous);
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

    if (!cmk_dyn_push(
            &module->functions,
            (CCUINT32)sizeof(CMKFunctionInfo *),
            (CCINT32)sizeof(CMKFunctionInfo *), &function
        ))
    {
        function_info_destroy(function);
        return sem_error(module, decl, "内存分配失败");
    }

    CMKScopeAddResult added = scope_add(
        module->module_scope, name, name_len, CMK_SYMBOL_FUNCTION,
        function->return_type, CCFALSE, function, NULL
    );
    if (added != CMK_SCOPE_ADD_OK)
    {
        return sem_error(
            module, decl,
            added == CMK_SCOPE_ADD_DUPLICATE
                ? "重复的函数声明：%.*s"
                : "内存分配失败",
            (int)name_len, name
        );
    }
    return CCTRUE;
}

static CCBOOL sem_predeclare_global(
    CMKSemanticModule *module, CMKASTNodeId decl
)
{
    const CMKASTNode *node = sem_node(module, decl);
    if (!node || node->kind != CMK_AST_VAR_DECL)
        return sem_error(module, decl, "内部错误：期望变量声明");

    CMKType type;
    if (!sem_type_from_ast(
            module, sem_child(module, decl, 0), CCFALSE, &type
        ))
        return CCFALSE;
    if (type.kind == CMK_VALUE_VOID)
        return sem_error(module, decl, "变量类型不能是 void");
    if (type.kind == CMK_VALUE_TARGET_REF ||
        (type.kind == CMK_VALUE_LIST &&
         type.element == CMK_VALUE_TARGET_REF))
        return sem_error(
            module, decl, "target_ref 只能用于 target 的 depends_on 字段"
        );

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.var_decl.name, &name, &name_len))
        return sem_error(module, decl, "变量名无效");

    CMKScopeAddResult added = scope_add(
        module->module_scope, name, name_len, CMK_SYMBOL_VAR,
        type, node->as.var_decl.is_const, NULL, NULL
    );
    if (added != CMK_SCOPE_ADD_OK)
    {
        return sem_error(
            module, decl,
            added == CMK_SCOPE_ADD_DUPLICATE
                ? "重复的变量声明：%.*s"
                : "内存分配失败",
            (int)name_len, name
        );
    }
    return CCTRUE;
}

/* ============================================================================
 * target 预收集
 * ========================================================================== */

static CCBOOL sem_collect_targets(
    CMKSemanticModule *module, CMKASTNodeId id, CCBOOL conditional
);

static CCBOOL sem_collect_target(
    CMKSemanticModule *module, CMKASTNodeId decl, CCBOOL conditional
)
{
    const CMKASTNode *node = sem_node(module, decl);
    if (!node || node->kind != CMK_AST_TARGET_DECL)
        return sem_error(module, decl, "内部错误：期望 target 声明");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.target_decl.name, &name, &name_len))
        return sem_error(module, decl, "target 名无效");

    const CMKTargetInfo *existing =
        sem_find_target_name(module, name, name_len);
    if (existing)
    {
        if (!existing->conditional || !conditional)
            return sem_error(
                module, decl, "重复的 target 声明：%.*s",
                (int)name_len, name
            );
    }

    CMKTargetInfo *target =
        ccalloc(NULL, sizeof(CMKTargetInfo), CMK_SEM_COMMENT);
    if (!target)
        return sem_error(module, decl, "内存分配失败");
    memset(target, 0, sizeof(*target));
    target->decl = decl;
    target->module = module;
    target->conditional = conditional;
    if (!cmk_string_set(&target->name, name, name_len))
    {
        target_info_destroy(target);
        return sem_error(module, decl, "内存分配失败");
    }
    if (!cmk_dyn_push(
            &module->targets, (CCUINT32)sizeof(CMKTargetInfo *),
            (CCINT32)sizeof(CMKTargetInfo *), &target
        ))
    {
        target_info_destroy(target);
        return sem_error(module, decl, "内存分配失败");
    }
    return CCTRUE;
}

typedef struct cmk_sem_target_work {
    CMKASTNodeId id;
    CCBOOL conditional;
} CMKSemTargetWork;

static CCBOOL sem_collect_targets(
    CMKSemanticModule *module, CMKASTNodeId id, CCBOOL conditional
)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CMKSemTargetWork root;
    root.id = id;
    root.conditional = conditional;
    if (!cmk_dyn_push(
            &stack, (CCUINT32)sizeof(CMKSemTargetWork),
            (CCINT32)sizeof(CMKSemTargetWork), &root
        ))
        return sem_error(module, id, "内存分配失败");

    while (stack.size >= (CCUINT32)sizeof(CMKSemTargetWork))
    {
        CMKSemTargetWork work;
        memset(&work, 0, sizeof(work));
        if (cc_dyn_core_pop(
                &stack, (CCUINT32)sizeof(CMKSemTargetWork),
                (CCINT32)sizeof(CMKSemTargetWork), &work
            ) != 0)
            goto Failed;

        const CMKASTNode *node = sem_node(module, work.id);
        if (!node)
        {
            (void)sem_error(module, work.id, "内部错误：节点缺失");
            goto Failed;
        }

        if (node->kind == CMK_AST_TARGET_DECL)
        {
            if (!sem_collect_target(module, work.id, work.conditional))
                goto Failed;
            continue;
        }

        if (node->kind == CMK_AST_IF)
        {
            if (node->as.if_stmt.has_else)
            {
                CMKSemTargetWork next;
                next.id = sem_child(module, work.id, 2);
                next.conditional = CCTRUE;
                if (!cmk_dyn_push(
                        &stack, (CCUINT32)sizeof(CMKSemTargetWork),
                        (CCINT32)sizeof(CMKSemTargetWork), &next
                    ))
                    goto Failed;
            }
            CMKSemTargetWork next;
            next.id = sem_child(module, work.id, 1);
            next.conditional = CCTRUE;
            if (!cmk_dyn_push(
                    &stack, (CCUINT32)sizeof(CMKSemTargetWork),
                    (CCINT32)sizeof(CMKSemTargetWork), &next
                ))
                goto Failed;
            continue;
        }

        if (node->kind == CMK_AST_BLOCK)
        {
            CCUINT32 count = sem_child_count(module, work.id);
            for (CCUINT32 i = count; i > 0; i--)
            {
                CMKSemTargetWork next;
                next.id = sem_child(module, work.id, i - 1);
                next.conditional = work.conditional;
                if (!cmk_dyn_push(
                        &stack, (CCUINT32)sizeof(CMKSemTargetWork),
                        (CCINT32)sizeof(CMKSemTargetWork), &next
                    ))
                    goto Failed;
            }
            continue;
        }

        (void)sem_error(
            module, work.id, "顶层条件分支只能包含 target 声明"
        );
        goto Failed;
    }

    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return CCTRUE;

Failed:
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return CCFALSE;
}

/* ============================================================================
 * 表达式
 * ========================================================================== */

static CCBOOL sem_expr(
    CMKSemanticModule *module, CMKASTNodeId id,
    CMKScope *scope, const CMKType *expected,
    CCBOOL allow_target_ref, CMKType *out
);

static void sem_set_type(
    CMKSemanticModule *module, CMKASTNodeId id, const CMKType *type
)
{
    if (!module || !module->nodes || id >= module->node_count || !type)
        return;
    module->nodes[id].type = *type;
}

static CCBOOL sem_expr_literal(
    CMKSemanticModule *module, CMKASTNodeId id, CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_LITERAL)
        return sem_error(module, id, "内部错误：期望字面量");

    switch (node->as.literal.kind)
    {
    case CMK_LITERAL_INT:
        *out = cmk_type_scalar(CMK_VALUE_INT);
        return CCTRUE;

    case CMK_LITERAL_FLOAT:
        *out = cmk_type_scalar(CMK_VALUE_FLOAT);
        return CCTRUE;

    case CMK_LITERAL_STRING:
        *out = cmk_type_scalar(CMK_VALUE_STRING);
        return CCTRUE;

    case CMK_LITERAL_BOOL:
        *out = cmk_type_scalar(CMK_VALUE_BOOL);
        return CCTRUE;

    default:
        return sem_error(module, id, "未知字面量类型");
    }
}

static CCBOOL sem_expr_config(
    CMKSemanticModule *module, CMKASTNodeId id,
    const CMKType *expected, CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_CONFIG_REF)
        return sem_error(module, id, "内部错误：期望配置引用");

    const char *path = NULL;
    CCUINT32 path_len = 0;
    if (!sem_string(module, node->as.config_ref.path, &path, &path_len))
        return sem_error(module, id, "配置引用路径无效");

    CMKType type;
    CMKConfigResult result = cmk_config_type(module->config, path, &type);
    switch (result)
    {
    case CMK_CONFIG_OK:
        break;

    case CMK_CONFIG_MISSING:
        return sem_error(module, id, "配置字段不存在：%s", path);

    case CMK_CONFIG_MIXED:
        return sem_error(module, id, "配置数组元素类型不一致：%s", path);

    case CMK_CONFIG_NOMEM:
        return sem_error(module, id, "配置查询内存分配失败");

    default:
        return sem_error(
            module, id, "配置字段不是可用的标量或同质数组：%s", path
        );
    }

    if (type.kind == CMK_VALUE_LIST &&
        type.element == CMK_VALUE_VOID &&
        expected && expected->kind == CMK_VALUE_LIST)
    {
        type.element = expected->element;
    }
    *out = type;
    return CCTRUE;
}

#if 0
static CCBOOL sem_expr_list(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKType *expected, CCBOOL allow_target_ref, CMKType *out
)
{
    CMKValueType expected_element =
        (expected && expected->kind == CMK_VALUE_LIST)
            ? expected->element : CMK_VALUE_VOID;

    CCUINT32 count = sem_child_count(module, id);
    CMKValueType element = expected_element;
    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKASTNodeId item = sem_child(module, id, i);
        CMKType item_expected;
        const CMKType *item_expected_ptr = NULL;
        if (element != CMK_VALUE_VOID)
        {
            item_expected = cmk_type_scalar(element);
            item_expected_ptr = &item_expected;
        }

        CMKType item_type;
        if (!sem_expr(
                module, item, scope, item_expected_ptr,
                allow_target_ref, &item_type
            ))
            return CCFALSE;
        if (item_type.kind == CMK_VALUE_LIST)
            return sem_error(module, item, "不允许嵌套 list");
        if (item_type.kind == CMK_VALUE_VOID)
            return sem_error(module, item, "list 元素不能是 void");

        if (element == CMK_VALUE_VOID)
            element = item_type.kind;
        else if (element != item_type.kind)
            return sem_error(module, item, "list 元素类型不一致");
    }

    *out = cmk_type_list(element);
    return CCTRUE;
}
#endif

static CCBOOL sem_expr_ident(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CCBOOL allow_target_ref, CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_IDENT)
        return sem_error(module, id, "内部错误：期望标识符");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.ident.name, &name, &name_len))
        return sem_error(module, id, "标识符无效");

    const CMKSymbol *symbol = scope_find(scope, name, name_len);
    if (symbol)
    {
        if (symbol->kind == CMK_SYMBOL_FUNCTION)
            return sem_error(module, id, "函数名不能作为值使用：%s", name);
        if (symbol->kind == CMK_SYMBOL_TARGET)
        {
            if (!allow_target_ref)
                return sem_error(
                    module, id,
                    "target 引用只能用于 target 的 depends_on 字段"
                );
            module->nodes[id].target_ref = symbol->target;
            *out = cmk_type_scalar(CMK_VALUE_TARGET_REF);
            sem_set_type(module, id, out);
            return CCTRUE;
        }
        *out = symbol->type;
        return CCTRUE;
    }

    if (allow_target_ref)
    {
        const CMKTargetInfo *target =
            sem_find_target_name(module, name, name_len);
        if (target)
        {
            module->nodes[id].target_ref = target;
            *out = cmk_type_scalar(CMK_VALUE_TARGET_REF);
            return CCTRUE;
        }
    }

    return sem_error(module, id, "未定义的标识符：%s", name);
}

static CCBOOL sem_expr_member(
    CMKSemanticModule *module, CMKASTNodeId id,
    CCBOOL allow_target_ref, CMKType *out
)
{
    if (!allow_target_ref)
        return sem_error(
            module, id, "成员访问只允许用于 depends_on 中的目标引用"
        );

    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_MEMBER)
        return sem_error(module, id, "内部错误：期望成员访问");

    CMKASTNodeId base_id = sem_child(module, id, 0);
    const CMKASTNode *base = sem_node(module, base_id);
    if (!base || base->kind != CMK_AST_IDENT)
        return sem_error(module, id, "目标引用格式必须是 别名.目标名");

    const char *alias = NULL;
    CCUINT32 alias_len = 0;
    const char *target_name = NULL;
    CCUINT32 target_name_len = 0;
    if (!sem_string(
            module, base->as.ident.name, &alias, &alias_len
        ) ||
        !sem_string(
            module, node->as.member.member,
            &target_name, &target_name_len
        ))
        return sem_error(module, id, "目标引用名称无效");

    CMKImportedTarget **targets =
        (CMKImportedTarget **)module->imported_targets.array;
    CCUINT32 count = module->imported_targets.size /
        (CCUINT32)sizeof(CMKImportedTarget *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKImportedTarget *record = targets[i];
        if (record->alias.len == alias_len &&
            target_name_len == record->target->name.len &&
            memcmp(record->alias.data, alias, alias_len) == 0 &&
            memcmp(
                record->target->name.data, target_name, target_name_len
            ) == 0)
        {
            module->nodes[id].target_ref = record->target;
            *out = cmk_type_scalar(CMK_VALUE_TARGET_REF);
            return CCTRUE;
        }
    }
    return sem_error(
        module, id, "未导入的目标引用：%.*s.%.*s",
        (int)alias_len, alias, (int)target_name_len, target_name
    );
}

typedef enum cmk_sem_expr_kind {
    CMK_SEM_EXPR_LITERAL = 0,
    CMK_SEM_EXPR_IDENT,
    CMK_SEM_EXPR_CONFIG,
    CMK_SEM_EXPR_MEMBER,
    CMK_SEM_EXPR_LIST,
    CMK_SEM_EXPR_UNARY,
    CMK_SEM_EXPR_BINARY,
    CMK_SEM_EXPR_CALL,
    CMK_SEM_EXPR_INDEX
} CMKSemExprKind;

typedef enum cmk_sem_expr_stage {
    CMK_SEM_EXPR_START = 0,
    CMK_SEM_EXPR_LIST_ELEM,
    CMK_SEM_EXPR_UNARY_OPERAND,
    CMK_SEM_EXPR_BINARY_LEFT,
    CMK_SEM_EXPR_BINARY_RIGHT,
    CMK_SEM_EXPR_CALL_ARG,
    CMK_SEM_EXPR_INDEX_BASE,
    CMK_SEM_EXPR_INDEX_INDEX
} CMKSemExprStage;

typedef struct cmk_sem_expr_frame {
    CMKSemExprKind kind;
    CMKSemExprStage stage;
    CMKASTNodeId id;
    CMKScope *scope;
    CMKType expected;
    CCBOOL has_expected;
    CCBOOL allow_target_ref;
    CMKType out;
    CCUINT32 index;
    CCUINT32 count;
    CMKType child_expected;
    CCBOOL child_has_expected;
    CMKType left_type;
    CMKValueType element;
    CMKBinaryOp op;
    const CMKFunctionInfo *function;
    const CMKBuiltinSpec *builtin;
    CCUINT32 formal_count;
} CMKSemExprFrame;

static CMKSemExprFrame *sem_expr_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(CMKSemExprFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKSemExprFrame);
    return &((CMKSemExprFrame *)frames->array)[count - 1];
}

static CCBOOL sem_expr_push(
    CMKSemanticModule *module, CCDyn *frames,
    CMKASTNodeId id, CMKScope *scope,
    const CMKType *expected, CCBOOL allow_target_ref
)
{
    CMKSemExprFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = CMK_SEM_EXPR_LITERAL;
    frame.stage = CMK_SEM_EXPR_START;
    frame.id = id;
    frame.scope = scope;
    frame.allow_target_ref = allow_target_ref;
    if (expected)
    {
        frame.expected = *expected;
        frame.has_expected = CCTRUE;
    }
    if (!cmk_dyn_push(
            frames, (CCUINT32)sizeof(CMKSemExprFrame),
            (CCINT32)sizeof(CMKSemExprFrame), &frame
        ))
        return sem_error(module, id, "内存分配失败");
    return CCTRUE;
}

static CCBOOL sem_expr_pop(CCDyn *frames, CMKSemExprFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(CMKSemExprFrame),
        (CCINT32)sizeof(CMKSemExprFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void sem_expr_frames_abort(CCDyn *frames)
{
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL sem_expr_list_next(
    CMKSemanticModule *module, CMKSemExprFrame *frame,
    CCDyn *frames
)
{
    if (frame->index >= frame->count)
    {
        frame->out = cmk_type_list(frame->element);
        return CCTRUE;
    }

    CMKASTNodeId item = sem_child(module, frame->id, frame->index);
    CMKType expected;
    const CMKType *expected_ptr = NULL;
    if (frame->element != CMK_VALUE_VOID)
    {
        expected = cmk_type_scalar(frame->element);
        expected_ptr = &expected;
    }
    return sem_expr_push(
        module, frames, item, frame->scope,
        expected_ptr, frame->allow_target_ref
    );
}

static CCBOOL sem_expr_call_next(
    CMKSemanticModule *module, CMKSemExprFrame *frame,
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
            if (frame->builtin->flags & CMK_BUILTIN_NORETURN)
                module->nodes[frame->id].noreturn = CCTRUE;
        }
        return CCTRUE;
    }

    CMKASTNodeId arg = sem_child(module, frame->id, frame->index);
    CMKType formal;
    const CMKType *expected = NULL;
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
    return sem_expr_push(module, frames, arg, frame->scope, expected, CCFALSE);
}

static CCBOOL sem_expr_begin_call(
    CMKSemanticModule *module, CMKSemExprFrame *frame,
    const CMKASTNode *node
)
{
    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.call.callee, &name, &name_len))
        return sem_error(module, frame->id, "函数名无效");

    const CMKSymbol *symbol = scope_find(frame->scope, name, name_len);
    if (symbol && symbol->kind == CMK_SYMBOL_VAR)
        return sem_error(
            module, frame->id, "变量不是函数：%.*s",
            (int)name_len, name
        );
    if (symbol && symbol->kind == CMK_SYMBOL_TARGET)
        return sem_error(
            module, frame->id, "target 不是函数：%.*s",
            (int)name_len, name
        );

    frame->count = sem_child_count(module, frame->id);
    frame->index = 0;
    if (symbol && symbol->kind == CMK_SYMBOL_FUNCTION)
    {
        const CMKFunctionInfo *function = symbol->function;
        if (!function)
            return sem_error(module, frame->id, "函数符号损坏");
        if (function->param_count != frame->count)
            return sem_error(
                module, frame->id,
                "函数 %.*s 需要 %u 个参数，实际 %u",
                (int)name_len, name, function->param_count, frame->count
            );
        frame->function = function;
        frame->stage = CMK_SEM_EXPR_CALL_ARG;
        return CCTRUE;
    }

    const CMKBuiltinSpec *spec =
        cmk_builtin_find(module->builtins, name, name_len);
    if (!spec)
        return sem_error(
            module, frame->id, "未定义的函数：%.*s", (int)name_len, name
        );
    if (frame->count < spec->min_args ||
        (!(spec->flags & CMK_BUILTIN_VARIADIC) &&
         frame->count > spec->max_args))
        return sem_error(
            module, frame->id,
            "内建函数调用参数数量错误：%s", spec->signature
        );
    frame->builtin = spec;
    frame->formal_count =
        (spec->flags & CMK_BUILTIN_VARIADIC)
            ? spec->min_args : spec->max_args;
    frame->stage = CMK_SEM_EXPR_CALL_ARG;
    return CCTRUE;
}

static CCBOOL sem_expr_apply_binary(
    CMKSemanticModule *module, CMKSemExprFrame *frame,
    const CMKType *left_type, const CMKType *right_type,
    CMKType *out
)
{
    CMKBinaryOp op = frame->op;
    if (op == CMK_BINARY_AND || op == CMK_BINARY_OR)
    {
        CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
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

    if (op == CMK_BINARY_EQ || op == CMK_BINARY_NE ||
        op == CMK_BINARY_LT || op == CMK_BINARY_LE ||
        op == CMK_BINARY_GT || op == CMK_BINARY_GE)
    {
        if (!cmk_type_equal(left_type, right_type))
            return sem_error(module, frame->id, "比较运算两侧类型不一致");
        if (left_type->kind == CMK_VALUE_VOID ||
            left_type->kind == CMK_VALUE_LIST ||
            left_type->kind == CMK_VALUE_ACTION ||
            left_type->kind == CMK_VALUE_TARGET_REF)
            return sem_error(module, frame->id, "该类型不能参与比较");
        if (op != CMK_BINARY_EQ && op != CMK_BINARY_NE &&
            left_type->kind != CMK_VALUE_INT &&
            left_type->kind != CMK_VALUE_FLOAT &&
            left_type->kind != CMK_VALUE_STRING)
            return sem_error(module, frame->id, "该类型不能参与大小比较");
        *out = cmk_type_scalar(CMK_VALUE_BOOL);
        return CCTRUE;
    }

    if (op == CMK_BINARY_ADD)
    {
        if (left_type->kind == CMK_VALUE_INT &&
            right_type->kind == CMK_VALUE_INT)
        {
            *out = cmk_type_scalar(CMK_VALUE_INT);
            return CCTRUE;
        }
        if (left_type->kind == CMK_VALUE_FLOAT &&
            right_type->kind == CMK_VALUE_FLOAT)
        {
            *out = cmk_type_scalar(CMK_VALUE_FLOAT);
            return CCTRUE;
        }
        if (left_type->kind == CMK_VALUE_STRING &&
            right_type->kind == CMK_VALUE_STRING)
        {
            *out = cmk_type_scalar(CMK_VALUE_STRING);
            return CCTRUE;
        }
        if (left_type->kind == CMK_VALUE_LIST &&
            right_type->kind == CMK_VALUE_LIST)
        {
            CMKValueType element = left_type->element;
            if (element == CMK_VALUE_VOID)
                element = right_type->element;
            if (element == CMK_VALUE_VOID)
                return sem_error(
                    module, frame->id, "空列表无法推导元素类型"
                );
            if (left_type->element != CMK_VALUE_VOID &&
                right_type->element != CMK_VALUE_VOID &&
                left_type->element != right_type->element)
                return sem_error(
                    module, frame->id, "list 拼接元素类型不一致"
                );
            *out = cmk_type_list(element);
            return CCTRUE;
        }
        return sem_error(module, frame->id, "加法两侧类型不支持");
    }

    if (!cmk_type_is_numeric(left_type) ||
        !cmk_type_is_numeric(right_type) ||
        left_type->kind != right_type->kind)
        return sem_error(module, frame->id, "算术运算需要同类型数值");
    if (op == CMK_BINARY_MOD && left_type->kind != CMK_VALUE_INT)
        return sem_error(module, frame->id, "取模运算只支持 int");
    *out = *left_type;
    return CCTRUE;
}

static CCBOOL sem_expr_step(
    CMKSemanticModule *module, CMKSemExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, const CMKType *input,
    CMKType *out, CCBOOL *done
)
{
    if (!has_input)
    {
        const CMKASTNode *node = sem_node(module, frame->id);
        if (!node)
            return sem_error(module, frame->id, "表达式节点缺失");

        switch (node->kind)
        {
        case CMK_AST_LITERAL:
            *done = CCTRUE;
            return sem_expr_literal(module, frame->id, out);

        case CMK_AST_IDENT:
            *done = CCTRUE;
            return sem_expr_ident(
                module, frame->id, frame->scope,
                frame->allow_target_ref, out
            );

        case CMK_AST_CONFIG_REF:
            *done = CCTRUE;
            return sem_expr_config(
                module, frame->id,
                frame->has_expected ? &frame->expected : NULL, out
            );

        case CMK_AST_MEMBER:
            *done = CCTRUE;
            return sem_expr_member(
                module, frame->id, frame->allow_target_ref, out
            );

        case CMK_AST_LIST:
            frame->kind = CMK_SEM_EXPR_LIST;
            frame->count = sem_child_count(module, frame->id);
            frame->index = 0;
            frame->element =
                (frame->has_expected &&
                 frame->expected.kind == CMK_VALUE_LIST)
                    ? frame->expected.element : CMK_VALUE_VOID;
            frame->stage = CMK_SEM_EXPR_LIST_ELEM;
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

        case CMK_AST_UNARY:
        {
            frame->kind = CMK_SEM_EXPR_UNARY;
            frame->stage = CMK_SEM_EXPR_UNARY_OPERAND;
            CMKASTNodeId operand = sem_child(module, frame->id, 0);
            CMKType expected;
            const CMKType *expected_ptr = NULL;
            if (node->as.unary.op == CMK_UNARY_NOT)
            {
                expected = cmk_type_scalar(CMK_VALUE_BOOL);
                expected_ptr = &expected;
            }
            else if (frame->has_expected &&
                     cmk_type_is_numeric(&frame->expected))
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
                    expected_ptr, CCFALSE
                ))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        case CMK_AST_BINARY:
        {
            frame->kind = CMK_SEM_EXPR_BINARY;
            frame->op = node->as.binary.op;
            frame->stage = CMK_SEM_EXPR_BINARY_LEFT;
            CMKType expected;
            const CMKType *expected_ptr = NULL;
            if (frame->op == CMK_BINARY_AND || frame->op == CMK_BINARY_OR)
            {
                expected = cmk_type_scalar(CMK_VALUE_BOOL);
                expected_ptr = &expected;
            }
            else if (frame->op != CMK_BINARY_EQ &&
                     frame->op != CMK_BINARY_NE &&
                     frame->op != CMK_BINARY_LT &&
                     frame->op != CMK_BINARY_LE &&
                     frame->op != CMK_BINARY_GT &&
                     frame->op != CMK_BINARY_GE &&
                     frame->has_expected &&
                     frame->expected.kind != CMK_VALUE_VOID)
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
                    frame->scope, expected_ptr, CCFALSE
                ))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }

        case CMK_AST_CALL:
            frame->kind = CMK_SEM_EXPR_CALL;
            frame->stage = CMK_SEM_EXPR_CALL_ARG;
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

        case CMK_AST_INDEX:
            frame->kind = CMK_SEM_EXPR_INDEX;
            frame->stage = CMK_SEM_EXPR_INDEX_BASE;
            if (!sem_expr_push(
                    module, frames, sem_child(module, frame->id, 0),
                    frame->scope, NULL, CCFALSE
                ))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;

        default:
            return sem_error(
                module, frame->id,
                "不支持的表达式节点：%s",
                cmk_ast_kind_name(node->kind)
            );
        }
    }

    switch (frame->kind)
    {
    case CMK_SEM_EXPR_LIST:
    {
        CMKASTNodeId item = sem_child(module, frame->id, frame->index);
        if (input->kind == CMK_VALUE_LIST)
            return sem_error(module, item, "不允许嵌套 list");
        if (input->kind == CMK_VALUE_VOID)
            return sem_error(module, item, "list 元素不能是 void");
        if (frame->element == CMK_VALUE_VOID)
            frame->element = input->kind;
        else if (frame->element != input->kind)
            return sem_error(module, item, "list 元素类型不一致");
        frame->index++;
        if (frame->index >= frame->count)
        {
            *out = cmk_type_list(frame->element);
            *done = CCTRUE;
            return CCTRUE;
        }
        if (!sem_expr_list_next(module, frame, frames))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    case CMK_SEM_EXPR_UNARY:
    {
        const CMKASTNode *node = sem_node(module, frame->id);
        if (!node || node->kind != CMK_AST_UNARY)
            return sem_error(module, frame->id, "内部错误：期望一元表达式");
        CMKASTNodeId operand = sem_child(module, frame->id, 0);
        if (node->as.unary.op == CMK_UNARY_NOT)
        {
            CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
            if (!sem_type_expect(
                    module, operand, input, &bool_type, "逻辑非"
                ))
                return CCFALSE;
            *out = bool_type;
            *done = CCTRUE;
            return CCTRUE;
        }
        if (!cmk_type_is_numeric(input))
            return sem_error(
                module, frame->id, "一元 + / - 只能作用于数值"
            );
        if (frame->has_expected &&
            cmk_type_is_numeric(&frame->expected) &&
            !sem_type_expect(
                module, frame->id, input, &frame->expected, "一元运算"
            ))
            return CCFALSE;
        *out = *input;
        *done = CCTRUE;
        return CCTRUE;
    }

    case CMK_SEM_EXPR_BINARY:
    {
        if (frame->stage == CMK_SEM_EXPR_BINARY_LEFT)
        {
            frame->left_type = *input;
            frame->stage = CMK_SEM_EXPR_BINARY_RIGHT;
            const CMKType *expected =
                frame->child_has_expected
                    ? &frame->child_expected : NULL;
            if (!sem_expr_push(
                    module, frames, sem_child(module, frame->id, 1),
                    frame->scope, expected, CCFALSE
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

    case CMK_SEM_EXPR_CALL:
    {
        CMKASTNodeId arg = sem_child(module, frame->id, frame->index);
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
            const CMKType *expected = NULL;
            CMKType formal;
            if (frame->index < frame->formal_count)
            {
                formal = frame->builtin->args[frame->index].type;
                expected = &formal;
            }
            if (expected && !cmk_builtin_arg_matches(expected, input))
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
                if (frame->builtin->flags & CMK_BUILTIN_NORETURN)
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

    case CMK_SEM_EXPR_INDEX:
    {
        if (frame->stage == CMK_SEM_EXPR_INDEX_BASE)
        {
            frame->left_type = *input;
            frame->stage = CMK_SEM_EXPR_INDEX_INDEX;
            CMKType int_type = cmk_type_scalar(CMK_VALUE_INT);
            if (!sem_expr_push(
                    module, frames, sem_child(module, frame->id, 1),
                    frame->scope, &int_type, CCFALSE
                ))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }
        if (frame->left_type.kind != CMK_VALUE_LIST)
            return sem_error(module, frame->id, "索引只支持 list");
        if (frame->left_type.element == CMK_VALUE_VOID)
            return sem_error(
                module, frame->id, "空列表无法确定索引元素类型"
            );
        if (input->kind != CMK_VALUE_INT)
            return sem_error(module, frame->id, "list 索引必须是 int");
        *out = cmk_type_scalar(frame->left_type.element);
        *done = CCTRUE;
        return CCTRUE;
    }

    default:
        return CCFALSE;
    }
}

static CCBOOL sem_expr_run(
    CMKSemanticModule *module, CMKASTNodeId id,
    CMKScope *scope, const CMKType *expected,
    CCBOOL allow_target_ref, CMKType *out
)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CCBOOL has_completed = CCFALSE;
    CMKType completed;

    for (;;)
    {
        if (has_completed)
        {
            CMKSemExprFrame *frame = sem_expr_top(&frames);
            if (!frame)
            {
                *out = completed;
                ccalloc(frames.array, 0, NULL);
                return CCTRUE;
            }
            CMKType result;
            CCBOOL done = CCFALSE;
            if (!sem_expr_step(
                    module, frame, &frames, CCTRUE, &completed,
                    &result, &done
                ))
                goto Failed;
            if (done)
            {
                sem_set_type(module, frame->id, &result);
                CMKSemExprFrame popped;
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
                    module, &frames, id, scope, expected,
                    allow_target_ref
                ))
                goto Failed;
            continue;
        }

        CMKSemExprFrame *frame = sem_expr_top(&frames);
        if (!frame)
            goto Failed;
        CMKType result;
        CCBOOL done = CCFALSE;
        if (!sem_expr_step(
                module, frame, &frames, CCFALSE, NULL,
                &result, &done
            ))
            goto Failed;
        if (done)
        {
            sem_set_type(module, frame->id, &result);
            CMKSemExprFrame popped;
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

#if 0
static CCBOOL sem_expr_call(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_CALL)
        return sem_error(module, id, "内部错误：期望调用");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.call.callee, &name, &name_len))
        return sem_error(module, id, "函数名无效");

    const CMKSymbol *symbol = scope_find(scope, name, name_len);
    if (symbol && symbol->kind == CMK_SYMBOL_VAR)
        return sem_error(module, id, "变量不是函数：%.*s", (int)name_len, name);
    if (symbol && symbol->kind == CMK_SYMBOL_TARGET)
        return sem_error(module, id, "target 不是函数：%.*s", (int)name_len, name);

    CCUINT32 argc = sem_child_count(module, id);
    if (symbol && symbol->kind == CMK_SYMBOL_FUNCTION)
    {
        const CMKFunctionInfo *function = symbol->function;
        if (!function)
            return sem_error(module, id, "函数符号损坏");
        if (function->param_count != argc)
            return sem_error(
                module, id, "函数 %.*s 需要 %u 个参数，实际 %u",
                (int)name_len, name, function->param_count, argc
            );
        for (CCUINT32 i = 0; i < argc; i++)
        {
            CMKASTNodeId arg = sem_child(module, id, i);
            CMKType actual;
            if (!sem_expr(
                    module, arg, scope, &function->params[i],
                    CCFALSE, &actual
                ))
                return CCFALSE;
            if (!sem_type_expect(
                    module, arg, &actual, &function->params[i],
                    "函数实参"
                ))
                return CCFALSE;
        }
        *out = function->return_type;
        module->nodes[id].function = function;
        return CCTRUE;
    }

    const CMKBuiltinSpec *spec =
        cmk_builtin_find(module->builtins, name, name_len);
    if (!spec)
        return sem_error(module, id, "未定义的函数：%.*s", (int)name_len, name);

    if (argc < spec->min_args ||
        (!(spec->flags & CMK_BUILTIN_VARIADIC) && argc > spec->max_args))
        return sem_error(
            module, id, "内建函数调用参数数量错误：%s", spec->signature
        );

    CCUINT32 formal_count =
        (spec->flags & CMK_BUILTIN_VARIADIC)
            ? spec->min_args : spec->max_args;
    for (CCUINT32 i = 0; i < argc; i++)
    {
        CMKASTNodeId arg = sem_child(module, id, i);
        const CMKType *expected = NULL;
        CMKType formal;
        if (i < formal_count)
        {
            formal = spec->args[i].type;
            expected = &formal;
        }
        CMKType actual;
        if (!sem_expr(
                module, arg, scope, expected, CCFALSE, &actual
            ))
            return CCFALSE;
        if (expected && !cmk_builtin_arg_matches(expected, &actual))
        {
            char actual_buf[64];
            char expected_buf[64];
            sem_type_format(&actual, actual_buf, sizeof(actual_buf));
            sem_type_format(expected, expected_buf, sizeof(expected_buf));
            return sem_error(
                module, arg, "内建实参类型错误：需要 %s，实际 %s（%s）",
                expected_buf, actual_buf, spec->signature
            );
        }
    }

    *out = spec->return_type;
    module->nodes[id].builtin = spec;
    if (spec->flags & CMK_BUILTIN_NORETURN)
        module->nodes[id].noreturn = CCTRUE;
    return CCTRUE;
}

static CCBOOL sem_expr_unary(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKType *expected, CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_UNARY)
        return sem_error(module, id, "内部错误：期望一元表达式");

    CMKASTNodeId operand = sem_child(module, id, 0);
    CMKType operand_type;
    if (node->as.unary.op == CMK_UNARY_NOT)
    {
        CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
        if (!sem_expr(
                module, operand, scope, &bool_type, CCFALSE, &operand_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, operand, &operand_type, &bool_type, "逻辑非"
            ))
            return CCFALSE;
        *out = bool_type;
        return CCTRUE;
    }

    const CMKType *operand_expected = NULL;
    if (expected && cmk_type_is_numeric(expected))
        operand_expected = expected;
    if (!sem_expr(
            module, operand, scope, operand_expected, CCFALSE, &operand_type
        ))
        return CCFALSE;
    if (!cmk_type_is_numeric(&operand_type))
        return sem_error(module, id, "一元 + / - 只能作用于数值");
    if (expected && cmk_type_is_numeric(expected) &&
        !sem_type_expect(
            module, id, &operand_type, expected, "一元运算"
        ))
        return CCFALSE;
    *out = operand_type;
    return CCTRUE;
}

static CCBOOL sem_expr_binary(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKType *expected, CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_BINARY)
        return sem_error(module, id, "内部错误：期望二元表达式");

    CMKASTNodeId left_id = sem_child(module, id, 0);
    CMKASTNodeId right_id = sem_child(module, id, 1);
    CMKBinaryOp op = node->as.binary.op;

    if (op == CMK_BINARY_AND || op == CMK_BINARY_OR)
    {
        CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
        CMKType left_type;
        CMKType right_type;
        if (!sem_expr(
                module, left_id, scope, &bool_type, CCFALSE, &left_type
            ) ||
            !sem_expr(
                module, right_id, scope, &bool_type, CCFALSE, &right_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, left_id, &left_type, &bool_type, "逻辑运算"
            ) ||
            !sem_type_expect(
                module, right_id, &right_type, &bool_type, "逻辑运算"
            ))
            return CCFALSE;
        *out = bool_type;
        return CCTRUE;
    }

    if (op == CMK_BINARY_EQ || op == CMK_BINARY_NE ||
        op == CMK_BINARY_LT || op == CMK_BINARY_LE ||
        op == CMK_BINARY_GT || op == CMK_BINARY_GE)
    {
        CMKType left_type;
        CMKType right_type;
        if (!sem_expr(
                module, left_id, scope, NULL, CCFALSE, &left_type
            ) ||
            !sem_expr(
                module, right_id, scope, NULL, CCFALSE, &right_type
            ))
            return CCFALSE;
        if (!cmk_type_equal(&left_type, &right_type))
            return sem_error(module, id, "比较运算两侧类型不一致");
        if (left_type.kind == CMK_VALUE_VOID ||
            left_type.kind == CMK_VALUE_LIST ||
            left_type.kind == CMK_VALUE_ACTION ||
            left_type.kind == CMK_VALUE_TARGET_REF)
            return sem_error(module, id, "该类型不能参与比较");
        if (op != CMK_BINARY_EQ && op != CMK_BINARY_NE &&
            left_type.kind != CMK_VALUE_INT &&
            left_type.kind != CMK_VALUE_FLOAT &&
            left_type.kind != CMK_VALUE_STRING)
            return sem_error(module, id, "该类型不能参与大小比较");
        *out = cmk_type_scalar(CMK_VALUE_BOOL);
        return CCTRUE;
    }

    const CMKType *operand_expected = NULL;
    if (expected && expected->kind != CMK_VALUE_VOID)
        operand_expected = expected;
    CMKType left_type;
    CMKType right_type;
    if (!sem_expr(
            module, left_id, scope, operand_expected, CCFALSE, &left_type
        ) ||
        !sem_expr(
            module, right_id, scope, operand_expected, CCFALSE, &right_type
        ))
        return CCFALSE;

    if (op == CMK_BINARY_ADD)
    {
        if (left_type.kind == CMK_VALUE_INT &&
            right_type.kind == CMK_VALUE_INT)
        {
            *out = cmk_type_scalar(CMK_VALUE_INT);
            return CCTRUE;
        }
        if (left_type.kind == CMK_VALUE_FLOAT &&
            right_type.kind == CMK_VALUE_FLOAT)
        {
            *out = cmk_type_scalar(CMK_VALUE_FLOAT);
            return CCTRUE;
        }
        if (left_type.kind == CMK_VALUE_STRING &&
            right_type.kind == CMK_VALUE_STRING)
        {
            *out = cmk_type_scalar(CMK_VALUE_STRING);
            return CCTRUE;
        }
        if (left_type.kind == CMK_VALUE_LIST &&
            right_type.kind == CMK_VALUE_LIST)
        {
            CMKValueType element = left_type.element;
            if (element == CMK_VALUE_VOID)
                element = right_type.element;
            if (element == CMK_VALUE_VOID)
                return sem_error(module, id, "空列表无法推导元素类型");
            if (left_type.element != CMK_VALUE_VOID &&
                right_type.element != CMK_VALUE_VOID &&
                left_type.element != right_type.element)
                return sem_error(module, id, "list 拼接元素类型不一致");
            *out = cmk_type_list(element);
            return CCTRUE;
        }
        return sem_error(module, id, "加法两侧类型不支持");
    }

    if (!cmk_type_is_numeric(&left_type) ||
        !cmk_type_is_numeric(&right_type) ||
        left_type.kind != right_type.kind)
        return sem_error(module, id, "算术运算需要同类型数值");
    if (op == CMK_BINARY_MOD && left_type.kind != CMK_VALUE_INT)
        return sem_error(module, id, "取模运算只支持 int");
    *out = left_type;
    return CCTRUE;
}

static CCBOOL sem_expr_index(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CMKType *out
)
{
    CMKASTNodeId base = sem_child(module, id, 0);
    CMKASTNodeId index = sem_child(module, id, 1);
    CMKType base_type;
    CMKType index_type;
    if (!sem_expr(
            module, base, scope, NULL, CCFALSE, &base_type
        ) ||
        !sem_expr(
            module, index, scope,
            &(CMKType){CMK_VALUE_INT, CMK_VALUE_VOID},
            CCFALSE, &index_type
        ))
        return CCFALSE;

    if (base_type.kind != CMK_VALUE_LIST)
        return sem_error(module, id, "索引只支持 list");
    if (base_type.element == CMK_VALUE_VOID)
        return sem_error(module, id, "空列表无法确定索引元素类型");
    if (index_type.kind != CMK_VALUE_INT)
        return sem_error(module, id, "list 索引必须是 int");
    *out = cmk_type_scalar(base_type.element);
    return CCTRUE;
}

static CCBOOL sem_expr(
    CMKSemanticModule *module, CMKASTNodeId id,
    CMKScope *scope, const CMKType *expected,
    CCBOOL allow_target_ref, CMKType *out
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node)
        return sem_error(module, id, "内部错误：表达式节点缺失");

    CCBOOL ok = CCFALSE;
    switch (node->kind)
    {
    case CMK_AST_LITERAL:
        ok = sem_expr_literal(module, id, out);
        break;

    case CMK_AST_IDENT:
        ok = sem_expr_ident(module, id, scope, allow_target_ref, out);
        break;

    case CMK_AST_CONFIG_REF:
        ok = sem_expr_config(module, id, expected, out);
        break;

    case CMK_AST_LIST:
        ok = sem_expr_list(
            module, id, scope, expected, allow_target_ref, out
        );
        break;

    case CMK_AST_UNARY:
        ok = sem_expr_unary(module, id, scope, expected, out);
        break;

    case CMK_AST_BINARY:
        ok = sem_expr_binary(module, id, scope, expected, out);
        break;

    case CMK_AST_CALL:
        ok = sem_expr_call(module, id, scope, out);
        break;

    case CMK_AST_INDEX:
        ok = sem_expr_index(module, id, scope, out);
        break;

    case CMK_AST_MEMBER:
        ok = sem_expr_member(module, id, allow_target_ref, out);
        break;

    default:
        return sem_error(
            module, id, "不支持的表达式节点：%s",
            cmk_ast_kind_name(node->kind)
        );
    }

    if (ok)
        sem_set_type(module, id, out);
    return ok;
}
#endif

static CCBOOL sem_expr(
    CMKSemanticModule *module, CMKASTNodeId id,
    CMKScope *scope, const CMKType *expected,
    CCBOOL allow_target_ref, CMKType *out
)
{
    if (!module || !out)
        return CCFALSE;
    return sem_expr_run(
        module, id, scope, expected, allow_target_ref, out
    );
}

/* ============================================================================
 * 语句与控制流
 * ========================================================================== */

static CCBOOL sem_block(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CCBOOL create_scope, const CMKFunctionInfo *function,
    CCUINT32 loop_depth, CMKSemFlow *flow
);

static CCBOOL sem_stmt_tree(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CCBOOL create_scope, const CMKFunctionInfo *function,
    CCUINT32 loop_depth, CMKSemFlow *flow
);

static CCBOOL sem_declare_local(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CMKType *out_type
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_VAR_DECL)
        return sem_error(module, id, "内部错误：期望变量声明");

    CMKType type;
    if (!sem_type_from_ast(
            module, sem_child(module, id, 0), CCFALSE, &type
        ))
        return CCFALSE;
    if (type.kind == CMK_VALUE_VOID)
        return sem_error(module, id, "变量类型不能是 void");
    if (type.kind == CMK_VALUE_TARGET_REF ||
        (type.kind == CMK_VALUE_LIST &&
         type.element == CMK_VALUE_TARGET_REF))
        return sem_error(
            module, id, "target_ref 只能用于 target 的 depends_on 字段"
        );

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.var_decl.name, &name, &name_len))
        return sem_error(module, id, "变量名无效");

    CMKASTNodeId init = sem_child(module, id, 1);
    CMKType init_type;
    if (!sem_expr(
            module, init, scope, &type, CCFALSE, &init_type
        ))
        return CCFALSE;
    if (!sem_type_expect(module, init, &init_type, &type, "变量初始化"))
        return CCFALSE;

    CMKScopeAddResult added = scope_add(
        scope, name, name_len, CMK_SYMBOL_VAR, type,
        node->as.var_decl.is_const, NULL, NULL
    );
    if (added != CMK_SCOPE_ADD_OK)
    {
        return sem_error(
            module, id,
            added == CMK_SCOPE_ADD_DUPLICATE
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
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_ASSIGN)
        return sem_error(module, id, "内部错误：期望赋值语句");

    CMKASTNodeId target_id = sem_child(module, id, 0);
    CMKASTNodeId value_id = sem_child(module, id, 1);
    const CMKASTNode *target = sem_node(module, target_id);
    if (!target)
        return sem_error(module, id, "赋值目标缺失");

    if (target->kind == CMK_AST_DISCARD)
    {
        if (node->as.assign.op != CMK_ASSIGN_SET)
            return sem_error(module, id, "_ 不能使用 += ");
        CMKType value_type;
        if (!sem_expr(
                module, value_id, scope, NULL, CCFALSE, &value_type
            ))
            return CCFALSE;
        if (value_type.kind == CMK_VALUE_VOID)
            return sem_error(module, value_id, "void 值不能被赋值或丢弃");
        return CCTRUE;
    }
    if (target->kind != CMK_AST_IDENT)
        return sem_error(module, id, "赋值目标必须是变量或 _");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, target->as.ident.name, &name, &name_len))
        return sem_error(module, target_id, "变量名无效");
    const CMKSymbol *symbol = scope_find(scope, name, name_len);
    if (!symbol || symbol->kind != CMK_SYMBOL_VAR)
        return sem_error(module, target_id, "未定义的变量：%.*s",
                         (int)name_len, name);
    if (symbol->is_const)
        return sem_error(module, target_id, "const 变量不能重新赋值：%.*s",
                         (int)name_len, name);

    CMKType value_type;
    if (node->as.assign.op == CMK_ASSIGN_SET)
    {
        if (!sem_expr(
                module, value_id, scope, &symbol->type,
                CCFALSE, &value_type
            ))
            return CCFALSE;
        return sem_type_expect(
            module, value_id, &value_type, &symbol->type, "赋值"
        );
    }

    if (symbol->type.kind == CMK_VALUE_LIST)
    {
        CMKType element = cmk_type_scalar(symbol->type.element);
        if (!sem_expr(
                module, value_id, scope, &element, CCFALSE, &value_type
            ))
            return CCFALSE;
        return sem_type_expect(
            module, value_id, &value_type, &element, "list 追加"
        );
    }
    if (!cmk_type_is_numeric(&symbol->type))
        return sem_error(module, id, "+= 只支持数值和 list");
    if (!sem_expr(
            module, value_id, scope, &symbol->type,
            CCFALSE, &value_type
        ))
        return CCFALSE;
    return sem_type_expect(
        module, value_id, &value_type, &symbol->type, "+= 运算"
    );
}

static CCBOOL sem_stmt_expr_stmt(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CMKSemFlow *flow
)
{
    CMKASTNodeId expr = sem_child(module, id, 0);
    CMKType type;
    if (!sem_expr(module, expr, scope, NULL, CCFALSE, &type))
        return CCFALSE;
    if (type.kind != CMK_VALUE_VOID &&
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
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKFunctionInfo *function, CMKSemFlow *flow
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_RETURN)
        return sem_error(module, id, "内部错误：期望 return");

    if (!function)
        return sem_error(module, id, "return 只能出现在函数内");

    if (node->as.return_stmt.has_value)
    {
        if (function->return_type.kind == CMK_VALUE_VOID)
            return sem_error(module, id, "void 函数不能返回表达式");
        CMKASTNodeId value = sem_child(module, id, 0);
        CMKType value_type;
        if (!sem_expr(
                module, value, scope, &function->return_type,
                CCFALSE, &value_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, value, &value_type,
                &function->return_type, "return"
            ))
            return CCFALSE;
    }
    else if (function->return_type.kind != CMK_VALUE_VOID)
    {
        return sem_error(module, id, "非 void 函数必须返回值");
    }

    flow->reachable = CCFALSE;
    flow->returns = CCTRUE;
    return CCTRUE;
}

#if 0
static CCBOOL sem_stmt_if(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKFunctionInfo *function, CCUINT32 loop_depth,
    CMKSemFlow *flow
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_IF)
        return sem_error(module, id, "内部错误：期望 if");

    CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
    CMKType condition_type;
    if (!sem_expr(
            module, sem_child(module, id, 0), scope,
            &bool_type, CCFALSE, &condition_type
        ))
        return CCFALSE;
    if (!sem_type_expect(
            module, sem_child(module, id, 0), &condition_type,
            &bool_type, "if 条件"
        ))
        return CCFALSE;

    CMKSemFlow then_flow;
    if (!sem_stmt(
            module, sem_child(module, id, 1), scope,
            function, loop_depth, &then_flow
        ))
        return CCFALSE;

    if (!node->as.if_stmt.has_else)
    {
        flow->reachable = CCTRUE;
        flow->returns = CCFALSE;
        return CCTRUE;
    }

    CMKSemFlow else_flow;
    if (!sem_stmt(
            module, sem_child(module, id, 2), scope,
            function, loop_depth, &else_flow
        ))
        return CCFALSE;

    flow->reachable = (then_flow.reachable || else_flow.reachable)
        ? CCTRUE : CCFALSE;
    flow->returns = (then_flow.returns && else_flow.returns)
        ? CCTRUE : CCFALSE;
    return CCTRUE;
}

static CCBOOL sem_stmt_while(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKFunctionInfo *function, CCUINT32 loop_depth,
    CMKSemFlow *flow
)
{
    CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
    CMKType condition_type;
    if (!sem_expr(
            module, sem_child(module, id, 0), scope,
            &bool_type, CCFALSE, &condition_type
        ))
        return CCFALSE;
    if (!sem_type_expect(
            module, sem_child(module, id, 0), &condition_type,
            &bool_type, "while 条件"
        ))
        return CCFALSE;

    CMKSemFlow body_flow;
    if (!sem_stmt(
            module, sem_child(module, id, 1), scope,
            function, loop_depth + 1, &body_flow
        ))
        return CCFALSE;

    flow->reachable = CCTRUE;
    flow->returns = CCFALSE;
    return CCTRUE;
}

static CCBOOL sem_stmt_for(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKFunctionInfo *function, CCUINT32 loop_depth,
    CMKSemFlow *flow
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_FOR_IN)
        return sem_error(module, id, "内部错误：期望 for-in");

    CMKType element;
    if (!sem_type_from_ast(
            module, sem_child(module, id, 0), CCFALSE, &element
        ))
        return CCFALSE;
    if (element.kind == CMK_VALUE_VOID)
        return sem_error(module, id, "for-in 元素类型不能是 void");

    CMKType list_type = cmk_type_list(element.kind);
    CMKType iterable_type;
    if (!sem_expr(
            module, sem_child(module, id, 1), scope,
            &list_type, CCFALSE, &iterable_type
        ))
        return CCFALSE;
    if (!sem_type_expect(
            module, sem_child(module, id, 1), &iterable_type,
            &list_type, "for-in 可迭代对象"
        ))
        return CCFALSE;

    CMKScope *loop_scope = scope_create(scope);
    if (!loop_scope)
        return sem_error(module, id, "内存分配失败");

    const char *name = NULL;
    CCUINT32 name_len = 0;
    if (!sem_string(module, node->as.for_in.name, &name, &name_len))
    {
        scope_destroy(loop_scope);
        return sem_error(module, id, "for-in 变量名无效");
    }
    CMKScopeAddResult added = scope_add(
        loop_scope, name, name_len, CMK_SYMBOL_VAR,
        element, CCFALSE, NULL, NULL
    );
    if (added != CMK_SCOPE_ADD_OK)
    {
        scope_destroy(loop_scope);
        return sem_error(module, id, "for-in 变量声明失败");
    }

    CMKSemFlow body_flow;
    CCBOOL ok = sem_stmt(
        module, sem_child(module, id, 2), loop_scope,
        function, loop_depth + 1, &body_flow
    );
    scope_destroy(loop_scope);
    if (!ok)
        return CCFALSE;

    flow->reachable = CCTRUE;
    flow->returns = CCFALSE;
    return CCTRUE;
}

static CCBOOL sem_stmt(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKFunctionInfo *function, CCUINT32 loop_depth,
    CMKSemFlow *flow
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node)
        return sem_error(module, id, "内部错误：语句节点缺失");

    flow->reachable = CCTRUE;
    flow->returns = CCFALSE;

    switch (node->kind)
    {
    case CMK_AST_BLOCK:
        return sem_block(
            module, id, scope, CCTRUE, function, loop_depth, flow
        );

    case CMK_AST_VAR_DECL:
        return sem_declare_local(module, id, scope, NULL);

    case CMK_AST_ASSIGN:
        return sem_stmt_assign(module, id, scope);

    case CMK_AST_EXPR_STMT:
        return sem_stmt_expr_stmt(module, id, scope, flow);

    case CMK_AST_IF:
        return sem_stmt_if(
            module, id, scope, function, loop_depth, flow
        );

    case CMK_AST_WHILE:
        return sem_stmt_while(
            module, id, scope, function, loop_depth, flow
        );

    case CMK_AST_FOR_IN:
        return sem_stmt_for(
            module, id, scope, function, loop_depth, flow
        );

    case CMK_AST_RETURN:
        return sem_stmt_return(module, id, scope, function, flow);

    case CMK_AST_BREAK:
    case CMK_AST_CONTINUE:
        if (loop_depth == 0)
            return sem_error(module, id, "break/continue 只能出现在循环内");
        flow->reachable = CCFALSE;
        flow->returns = CCFALSE;
        return CCTRUE;

    default:
        return sem_error(
            module, id, "不支持的语句节点：%s",
            cmk_ast_kind_name(node->kind)
        );
    }
}
#endif

typedef enum cmk_sem_frame_kind {
    CMK_SEM_FRAME_BLOCK = 0,
    CMK_SEM_FRAME_IF,
    CMK_SEM_FRAME_WHILE,
    CMK_SEM_FRAME_FOR
} CMKSemFrameKind;

typedef enum cmk_sem_frame_stage {
    CMK_SEM_STAGE_ITEMS = 0,
    CMK_SEM_STAGE_THEN,
    CMK_SEM_STAGE_ELSE,
    CMK_SEM_STAGE_BODY
} CMKSemFrameStage;

typedef struct cmk_sem_frame {
    CMKSemFrameKind kind;
    CMKSemFrameStage stage;
    CMKASTNodeId id;
    CMKScope *scope;
    CCBOOL owns_scope;
    const CMKFunctionInfo *function;
    CCUINT32 loop_depth;
    CCUINT32 index;
    CCUINT32 count;
    CMKSemFlow flow;
    CMKSemFlow branch;
    CCBOOL has_else;
} CMKSemFrame;

static CCBOOL sem_stmt_simple(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    const CMKFunctionInfo *function, CCUINT32 loop_depth,
    CMKSemFlow *flow
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node)
        return sem_error(module, id, "内部错误：语句节点缺失");

    flow->reachable = CCTRUE;
    flow->returns = CCFALSE;
    switch (node->kind)
    {
    case CMK_AST_VAR_DECL:
        return sem_declare_local(module, id, scope, NULL);

    case CMK_AST_ASSIGN:
        return sem_stmt_assign(module, id, scope);

    case CMK_AST_EXPR_STMT:
        return sem_stmt_expr_stmt(module, id, scope, flow);

    case CMK_AST_RETURN:
        return sem_stmt_return(module, id, scope, function, flow);

    case CMK_AST_BREAK:
    case CMK_AST_CONTINUE:
        if (loop_depth == 0)
            return sem_error(module, id, "break/continue 只能出现在循环内");
        flow->reachable = CCFALSE;
        flow->returns = CCFALSE;
        return CCTRUE;

    default:
        return sem_error(
            module, id, "内部错误：不是简单语句：%s",
            cmk_ast_kind_name(node->kind)
        );
    }
}

static CCBOOL sem_frame_begin(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CCBOOL create_scope, const CMKFunctionInfo *function,
    CCUINT32 loop_depth, CMKSemFrame *frame,
    CCBOOL *is_simple, CMKSemFlow *simple_flow
)
{
    memset(frame, 0, sizeof(*frame));
    *is_simple = CCFALSE;

    const CMKASTNode *node = sem_node(module, id);
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
    case CMK_AST_BLOCK:
        frame->kind = CMK_SEM_FRAME_BLOCK;
        frame->stage = CMK_SEM_STAGE_ITEMS;
        if (create_scope)
        {
            CMKScope *block_scope = scope_create(scope);
            if (!block_scope)
                return sem_error(module, id, "内存分配失败");
            frame->scope = block_scope;
            frame->owns_scope = CCTRUE;
        }
        frame->count = sem_child_count(module, id);
        return CCTRUE;

    case CMK_AST_IF:
    {
        CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
        CMKType condition_type;
        if (!sem_expr(
                module, sem_child(module, id, 0), scope,
                &bool_type, CCFALSE, &condition_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, sem_child(module, id, 0), &condition_type,
                &bool_type, "if 条件"
            ))
            return CCFALSE;
        frame->kind = CMK_SEM_FRAME_IF;
        frame->stage = CMK_SEM_STAGE_THEN;
        frame->has_else = node->as.if_stmt.has_else;
        return CCTRUE;
    }

    case CMK_AST_WHILE:
    {
        CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
        CMKType condition_type;
        if (!sem_expr(
                module, sem_child(module, id, 0), scope,
                &bool_type, CCFALSE, &condition_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, sem_child(module, id, 0), &condition_type,
                &bool_type, "while 条件"
            ))
            return CCFALSE;
        frame->kind = CMK_SEM_FRAME_WHILE;
        frame->stage = CMK_SEM_STAGE_BODY;
        return CCTRUE;
    }

    case CMK_AST_FOR_IN:
    {
        CMKType element;
        if (!sem_type_from_ast(
                module, sem_child(module, id, 0), CCFALSE, &element
            ))
            return CCFALSE;
        if (element.kind == CMK_VALUE_VOID)
            return sem_error(module, id, "for-in 元素类型不能是 void");

        CMKType list_type = cmk_type_list(element.kind);
        CMKType iterable_type;
        if (!sem_expr(
                module, sem_child(module, id, 1), scope,
                &list_type, CCFALSE, &iterable_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, sem_child(module, id, 1), &iterable_type,
                &list_type, "for-in 可迭代对象"
            ))
            return CCFALSE;

        CMKScope *loop_scope = scope_create(scope);
        if (!loop_scope)
            return sem_error(module, id, "内存分配失败");

        const char *name = NULL;
        CCUINT32 name_len = 0;
        if (!sem_string(module, node->as.for_in.name, &name, &name_len))
        {
            scope_destroy(loop_scope);
            return sem_error(module, id, "for-in 变量名无效");
        }
        CMKScopeAddResult added = scope_add(
            loop_scope, name, name_len, CMK_SYMBOL_VAR,
            element, CCFALSE, NULL, NULL
        );
        if (added != CMK_SCOPE_ADD_OK)
        {
            scope_destroy(loop_scope);
            return sem_error(module, id, "for-in 变量声明失败");
        }

        frame->kind = CMK_SEM_FRAME_FOR;
        frame->stage = CMK_SEM_STAGE_BODY;
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

static CMKSemFrame *sem_frame_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(CMKSemFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKSemFrame);
    return &((CMKSemFrame *)frames->array)[count - 1];
}

static CCBOOL sem_frame_push(
    CMKSemanticModule *module, CCDyn *frames, const CMKSemFrame *frame
)
{
    if (!cmk_dyn_push(
            frames, (CCUINT32)sizeof(CMKSemFrame),
            (CCINT32)sizeof(CMKSemFrame), frame
        ))
        return sem_error(module, frame->id, "内存分配失败");
    return CCTRUE;
}

static CCBOOL sem_frame_pop(CCDyn *frames, CMKSemFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(CMKSemFrame),
        (CCINT32)sizeof(CMKSemFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void sem_frame_scope_release(CMKSemFrame *frame)
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
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKSemFrame);
    for (CCUINT32 i = count; i > 0; i--)
        sem_frame_scope_release(
            &((CMKSemFrame *)frames->array)[i - 1]
        );
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL sem_stmt_tree(
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CCBOOL create_scope, const CMKFunctionInfo *function,
    CCUINT32 loop_depth, CMKSemFlow *flow
)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CCBOOL has_completed = CCFALSE;
    CMKSemFlow completed;
    memset(&completed, 0, sizeof(completed));

    for (;;)
    {
        if (has_completed)
        {
            CMKSemFrame *frame = sem_frame_top(&frames);
            if (!frame)
            {
                *flow = completed;
                ccalloc(frames.array, 0, NULL);
                return CCTRUE;
            }

            if (frame->kind == CMK_SEM_FRAME_BLOCK)
            {
                if (!completed.reachable)
                {
                    frame->flow.reachable = CCFALSE;
                    frame->flow.returns = completed.returns;
                }
                has_completed = CCFALSE;
                continue;
            }

            if (frame->kind == CMK_SEM_FRAME_IF)
            {
                if (frame->stage == CMK_SEM_STAGE_THEN)
                {
                    frame->branch = completed;
                    if (frame->has_else)
                    {
                        frame->stage = CMK_SEM_STAGE_ELSE;
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
            CMKSemFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!sem_frame_pop(&frames, &popped))
                goto Failed;
            has_completed = CCTRUE;
            continue;
        }

        if (frames.size == 0)
        {
            CMKSemFrame start;
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

        CMKSemFrame *frame = sem_frame_top(&frames);
        if (!frame)
            goto Failed;

        CMKASTNodeId child_id = CMK_AST_NODE_NIL;
        CMKScope *child_scope = frame->scope;
        CCUINT32 child_loop_depth = frame->loop_depth;
        CCBOOL child_create_scope = CCTRUE;

        if (frame->kind == CMK_SEM_FRAME_BLOCK)
        {
            if (frame->index >= frame->count)
            {
                completed = frame->flow;
                sem_frame_scope_release(frame);
                CMKSemFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!sem_frame_pop(&frames, &popped))
                    goto Failed;
                has_completed = CCTRUE;
                continue;
            }
            child_id = sem_child(module, frame->id, frame->index);
            frame->index++;
        }
        else if (frame->kind == CMK_SEM_FRAME_IF)
        {
            child_id = sem_child(
                module, frame->id,
                frame->stage == CMK_SEM_STAGE_THEN ? 1 : 2
            );
        }
        else if (frame->kind == CMK_SEM_FRAME_WHILE)
        {
            child_id = sem_child(module, frame->id, 1);
            child_loop_depth = frame->loop_depth + 1;
        }
        else
        {
            child_id = sem_child(module, frame->id, 2);
        }

        CMKSemFrame child;
        CCBOOL is_simple = CCFALSE;
        CMKSemFlow simple_flow;
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
    CMKSemanticModule *module, CMKASTNodeId id, CMKScope *scope,
    CCBOOL create_scope, const CMKFunctionInfo *function,
    CCUINT32 loop_depth, CMKSemFlow *flow
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_BLOCK)
        return sem_error(module, id, "内部错误：期望代码块");
    return sem_stmt_tree(
        module, id, scope, create_scope, function, loop_depth, flow
    );
}

/* ============================================================================
 * 函数、target 与模块
 * ========================================================================== */

static CCBOOL sem_analyze_function(
    CMKSemanticModule *module, CMKFunctionInfo *function
)
{
    CMKScope *function_scope = scope_create(module->module_scope);
    if (!function_scope)
        return sem_error(module, function->decl, "内存分配失败");

    const CMKASTNode *decl = sem_node(module, function->decl);
    if (!decl)
    {
        scope_destroy(function_scope);
        return sem_error(module, function->decl, "函数声明缺失");
    }

    for (CCUINT32 i = 0; i < function->param_count; i++)
    {
        CMKASTNodeId param = sem_child(module, function->decl, i + 1);
        const CMKASTNode *param_node = sem_node(module, param);
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
        CMKScopeAddResult added = scope_add(
            function_scope, name, name_len, CMK_SYMBOL_VAR,
            function->params[i], CCFALSE, NULL, NULL
        );
        if (added != CMK_SCOPE_ADD_OK)
        {
            scope_destroy(function_scope);
            return sem_error(module, param, "重复的函数参数：%s", name);
        }
    }

    CMKASTNodeId body = sem_child(
        module, function->decl, function->param_count + 1
    );
    const CMKASTNode *body_node = sem_node(module, body);
    if (!body_node || body_node->kind != CMK_AST_BLOCK)
    {
        scope_destroy(function_scope);
        return sem_error(module, body, "函数体缺失");
    }

    CMKSemFlow flow;
    CCBOOL ok = sem_block(
        module, body, function_scope, CCFALSE,
        function, 0, &flow
    );
    scope_destroy(function_scope);
    if (!ok)
        return CCFALSE;

    if (function->return_type.kind != CMK_VALUE_VOID && flow.reachable)
        return sem_error(
            module, function->decl,
            "非 void 函数 %s 缺少返回值",
            function->name.data
        );
    return CCTRUE;
}

static CCBOOL sem_analyze_global(
    CMKSemanticModule *module, CMKASTNodeId decl
)
{
    const CMKASTNode *node = sem_node(module, decl);
    if (!node)
        return sem_error(module, decl, "变量声明缺失");
    CMKType type;
    if (!sem_type_from_ast(
            module, sem_child(module, decl, 0), CCFALSE, &type
        ))
        return CCFALSE;
    CMKType value_type;
    if (!sem_expr(
            module, sem_child(module, decl, 1), module->module_scope,
            &type, CCFALSE, &value_type
        ))
        return CCFALSE;
    return sem_type_expect(
        module, sem_child(module, decl, 1),
        &value_type, &type, "变量初始化"
    );
}

static CCBOOL sem_analyze_target(
    CMKSemanticModule *module, CMKASTNodeId decl
)
{
    const CMKASTNode *node = sem_node(module, decl);
    if (!node || node->kind != CMK_AST_TARGET_DECL)
        return sem_error(module, decl, "target 声明缺失");

    CCBOOL seen[4] = {CCFALSE, CCFALSE, CCFALSE, CCFALSE};
    CCUINT32 count = sem_child_count(module, decl);
    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKASTNodeId field = sem_child(module, decl, i);
        const CMKASTNode *field_node = sem_node(module, field);
        if (!field_node || field_node->kind != CMK_AST_TARGET_FIELD)
            return sem_error(module, field, "target 字段节点错误");

        CMKTargetFieldKind kind = field_node->as.target_field.field;
        if (kind < 0 || kind >= CMK_TARGET_FIELD_KIND_COUNT ||
            kind == CMK_TARGET_FIELD_UNKNOWN)
            return sem_error(module, field, "未知的 target 字段");

        if (seen[kind])
            return sem_error(module, field, "重复的 target 字段");
        seen[kind] = CCTRUE;

        CMKType expected;
        CCBOOL allow_target_ref = CCFALSE;
        switch (kind)
        {
        case CMK_TARGET_FIELD_DEPENDS_ON:
            expected = cmk_type_list(CMK_VALUE_TARGET_REF);
            allow_target_ref = CCTRUE;
            break;

        case CMK_TARGET_FIELD_INPUTS:
        case CMK_TARGET_FIELD_OUTPUTS:
            expected = cmk_type_list(CMK_VALUE_STRING);
            break;

        case CMK_TARGET_FIELD_ACTIONS:
            expected = cmk_type_list(CMK_VALUE_ACTION);
            break;

        default:
            return sem_error(module, field, "未知的 target 字段");
        }

        CMKType field_type;
        if (!sem_type_from_ast(
                module, sem_child(module, field, 0),
                allow_target_ref, &field_type
            ))
            return CCFALSE;
        if (!cmk_type_equal(&field_type, &expected))
            return sem_error(
                module, field, "target 字段类型必须与字段定义一致"
            );

        CMKType value_type;
        if (!sem_expr(
                module, sem_child(module, field, 1), module->module_scope,
                &expected, allow_target_ref, &value_type
            ))
            return CCFALSE;
        if (!sem_type_expect(
                module, sem_child(module, field, 1),
                &value_type, &expected, "target 字段值"
            ))
            return CCFALSE;
    }
    return CCTRUE;
}

#if 0
static CCBOOL sem_analyze_top_if(
    CMKSemanticModule *module, CMKASTNodeId id
)
{
    const CMKASTNode *node = sem_node(module, id);
    if (!node || node->kind != CMK_AST_IF)
        return sem_error(module, id, "顶层 if 节点错误");

    CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
    CMKType condition_type;
    if (!sem_expr(
            module, sem_child(module, id, 0), module->module_scope,
            &bool_type, CCFALSE, &condition_type
        ))
        return CCFALSE;
    if (!sem_type_expect(
            module, sem_child(module, id, 0), &condition_type,
            &bool_type, "顶层 if 条件"
        ))
        return CCFALSE;

    CMKASTNodeId then_branch = sem_child(module, id, 1);
    const CMKASTNode *then_node = sem_node(module, then_branch);
    if (!then_node)
        return sem_error(module, then_branch, "顶层 if 分支缺失");

    if (then_node->kind == CMK_AST_TARGET_DECL)
    {
        if (!sem_analyze_target(module, then_branch))
            return CCFALSE;
    }
    else if (then_node->kind == CMK_AST_IF)
    {
        if (!sem_analyze_top_if(module, then_branch))
            return CCFALSE;
    }
    else if (then_node->kind == CMK_AST_BLOCK)
    {
        CCUINT32 count = sem_child_count(module, then_branch);
        for (CCUINT32 i = 0; i < count; i++)
        {
            CMKASTNodeId child = sem_child(module, then_branch, i);
            const CMKASTNode *child_node = sem_node(module, child);
            if (!child_node)
                return sem_error(module, child, "顶层 if 分支缺失");
            if (child_node->kind == CMK_AST_TARGET_DECL)
            {
                if (!sem_analyze_target(module, child))
                    return CCFALSE;
            }
            else if (child_node->kind == CMK_AST_IF)
            {
                if (!sem_analyze_top_if(module, child))
                    return CCFALSE;
            }
            else
                return sem_error(
                    module, child, "顶层 if 分支只能包含 target 声明"
                );
        }
    }
    else
        return sem_error(
            module, then_branch, "顶层 if 分支只能包含 target 声明"
        );

    if (node->as.if_stmt.has_else)
    {
        CMKASTNodeId else_branch = sem_child(module, id, 2);
        const CMKASTNode *else_node = sem_node(module, else_branch);
        if (!else_node)
            return sem_error(module, else_branch, "顶层 if else 缺失");
        if (else_node->kind == CMK_AST_TARGET_DECL)
        {
            if (!sem_analyze_target(module, else_branch))
                return CCFALSE;
        }
        else if (else_node->kind == CMK_AST_IF)
        {
            if (!sem_analyze_top_if(module, else_branch))
                return CCFALSE;
        }
        else if (else_node->kind == CMK_AST_BLOCK)
        {
            CCUINT32 count = sem_child_count(module, else_branch);
            for (CCUINT32 i = 0; i < count; i++)
            {
                CMKASTNodeId child = sem_child(module, else_branch, i);
                const CMKASTNode *child_node = sem_node(module, child);
                if (!child_node)
                    return sem_error(module, child, "顶层 if 分支缺失");
                if (child_node->kind == CMK_AST_TARGET_DECL)
                {
                    if (!sem_analyze_target(module, child))
                        return CCFALSE;
                }
                else if (child_node->kind == CMK_AST_IF)
                {
                    if (!sem_analyze_top_if(module, child))
                        return CCFALSE;
                }
                else
                    return sem_error(
                        module, child, "顶层 if 分支只能包含 target 声明"
                    );
            }
        }
        else
            return sem_error(
                module, else_branch, "顶层 if 分支只能包含 target 声明"
            );
    }
    return CCTRUE;
}
#endif

static CCBOOL sem_analyze_top_if(
    CMKSemanticModule *module, CMKASTNodeId root
)
{
    const CMKASTNode *root_node = sem_node(module, root);
    if (!root_node || root_node->kind != CMK_AST_IF)
        return sem_error(module, root, "顶层 if 节点错误");

    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CMKASTNodeId first = root;
    if (!cmk_dyn_push(
            &stack, (CCUINT32)sizeof(CMKASTNodeId),
            (CCINT32)sizeof(CMKASTNodeId), &first
        ))
        return sem_error(module, root, "内存分配失败");

    while (stack.size >= (CCUINT32)sizeof(CMKASTNodeId))
    {
        CMKASTNodeId id = CMK_AST_NODE_NIL;
        if (cc_dyn_core_pop(
                &stack, (CCUINT32)sizeof(CMKASTNodeId),
                (CCINT32)sizeof(CMKASTNodeId), &id
            ) != 0)
            goto Failed;

        const CMKASTNode *node = sem_node(module, id);
        if (!node)
        {
            (void)sem_error(module, id, "顶层 if 分支缺失");
            goto Failed;
        }

        if (node->kind == CMK_AST_TARGET_DECL)
        {
            if (!sem_analyze_target(module, id))
                goto Failed;
            continue;
        }

        if (node->kind == CMK_AST_IF)
        {
            CMKType bool_type = cmk_type_scalar(CMK_VALUE_BOOL);
            CMKType condition_type;
            if (!sem_expr(
                    module, sem_child(module, id, 0),
                    module->module_scope, &bool_type, CCFALSE,
                    &condition_type
                ))
                goto Failed;
            if (!sem_type_expect(
                    module, sem_child(module, id, 0), &condition_type,
                    &bool_type, "顶层 if 条件"
                ))
                goto Failed;

            if (node->as.if_stmt.has_else)
            {
                CMKASTNodeId else_id = sem_child(module, id, 2);
                if (!cmk_dyn_push(
                        &stack, (CCUINT32)sizeof(CMKASTNodeId),
                        (CCINT32)sizeof(CMKASTNodeId), &else_id
                    ))
                    goto Failed;
            }
            CMKASTNodeId then_id = sem_child(module, id, 1);
            if (!cmk_dyn_push(
                    &stack, (CCUINT32)sizeof(CMKASTNodeId),
                    (CCINT32)sizeof(CMKASTNodeId), &then_id
                ))
                goto Failed;
            continue;
        }

        if (node->kind == CMK_AST_BLOCK)
        {
            CCUINT32 count = sem_child_count(module, id);
            for (CCUINT32 i = count; i > 0; i--)
            {
                CMKASTNodeId child = sem_child(module, id, i - 1);
                const CMKASTNode *child_node = sem_node(module, child);
                if (!child_node)
                {
                    (void)sem_error(module, child, "顶层 if 分支缺失");
                    goto Failed;
                }
                if (child_node->kind != CMK_AST_TARGET_DECL &&
                    child_node->kind != CMK_AST_IF)
                {
                    (void)sem_error(
                        module, child,
                        "顶层 if 分支只能包含 target 声明"
                    );
                    goto Failed;
                }
                if (!cmk_dyn_push(
                        &stack, (CCUINT32)sizeof(CMKASTNodeId),
                        (CCINT32)sizeof(CMKASTNodeId), &child
                    ))
                    goto Failed;
            }
            continue;
        }

        (void)sem_error(
            module, id, "顶层 if 分支只能包含 target 声明"
        );
        goto Failed;
    }

    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return CCTRUE;

Failed:
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return CCFALSE;
}

static CCBOOL sem_add_imported_functions(CMKSemanticModule *module)
{
    CMKImportedFunction **imports =
        (CMKImportedFunction **)module->imported_functions.array;
    CCUINT32 count = module->imported_functions.size /
        (CCUINT32)sizeof(CMKImportedFunction *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKImportedFunction *import = imports[i];
        if (!import || !import->function)
            return sem_error(module, cmk_ast_root(module->ast),
                             "导入函数记录损坏");
        if (cmk_builtin_find(
                module->builtins, import->name.data, import->name.len
            ))
            return sem_error(
                module, cmk_ast_root(module->ast),
                "导入函数不能与内建同名：%s", import->name.data
            );

        CMKScopeAddResult added = scope_add(
            module->module_scope,
            import->name.data, import->name.len,
            CMK_SYMBOL_FUNCTION, import->function->return_type,
            CCFALSE, import->function, NULL
        );
        if (added != CMK_SCOPE_ADD_OK)
        {
            return sem_error(
                module, cmk_ast_root(module->ast),
                added == CMK_SCOPE_ADD_DUPLICATE
                    ? "导入函数与本地符号重名：%s"
                    : "内存分配失败",
                import->name.data
            );
        }
    }
    return CCTRUE;
}

CCBOOL cmk_semantic_analyze(CMKSemanticModule *module)
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

    CMKASTNodeId root = cmk_ast_root(module->ast);
    CCUINT32 root_count = sem_child_count(module, root);

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        CMKASTNodeId item = sem_child(module, root, i);
        const CMKASTNode *node = sem_node(module, item);
        if (node && node->kind == CMK_AST_FUNC_DECL)
        {
            if (!sem_predeclare_function(module, item))
                goto Failed;
        }
    }

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        CMKASTNodeId item = sem_child(module, root, i);
        const CMKASTNode *node = sem_node(module, item);
        if (node && node->kind == CMK_AST_VAR_DECL)
        {
            if (!sem_predeclare_global(module, item))
                goto Failed;
        }
    }

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        CMKASTNodeId item = sem_child(module, root, i);
        const CMKASTNode *node = sem_node(module, item);
        if (node && (node->kind == CMK_AST_TARGET_DECL ||
                     node->kind == CMK_AST_IF))
        {
            if (!sem_collect_targets(module, item, CCFALSE))
                goto Failed;
        }
    }

    if (!sem_add_imported_functions(module))
        goto Failed;

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        CMKASTNodeId item = sem_child(module, root, i);
        const CMKASTNode *node = sem_node(module, item);
        if (node && node->kind == CMK_AST_VAR_DECL)
        {
            if (!sem_analyze_global(module, item))
                goto Failed;
        }
    }

    CMKFunctionInfo **functions =
        (CMKFunctionInfo **)module->functions.array;
    CCUINT32 function_count =
        module->functions.size / (CCUINT32)sizeof(CMKFunctionInfo *);
    for (CCUINT32 i = 0; i < function_count; i++)
    {
        if (!sem_analyze_function(module, functions[i]))
            goto Failed;
    }

    for (CCUINT32 i = 0; i < root_count; i++)
    {
        CMKASTNodeId item = sem_child(module, root, i);
        const CMKASTNode *node = sem_node(module, item);
        if (!node)
            goto Failed;
        if (node->kind == CMK_AST_TARGET_DECL)
        {
            if (!sem_analyze_target(module, item))
                goto Failed;
        }
        else if (node->kind == CMK_AST_IF)
        {
            if (!sem_analyze_top_if(module, item))
                goto Failed;
        }
    }

    module->analyzed = CCTRUE;
    module->ok = CCTRUE;
    return CCTRUE;

Failed:
    module->analyzed = CCTRUE;
    module->ok = CCFALSE;
    return CCFALSE;
}

const CMKType *cmk_semantic_node_type(
    const CMKSemanticModule *module, CMKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return NULL;
    return &module->nodes[node].type;
}

const CMKTargetInfo *cmk_semantic_node_target_ref(
    const CMKSemanticModule *module, CMKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return NULL;
    return module->nodes[node].target_ref;
}

const CMKFunctionInfo *cmk_semantic_node_function(
    const CMKSemanticModule *module, CMKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return NULL;
    return module->nodes[node].function;
}

const CMKBuiltinSpec *cmk_semantic_node_builtin(
    const CMKSemanticModule *module, CMKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return NULL;
    return module->nodes[node].builtin;
}

CCBOOL cmk_semantic_node_is_noreturn(
    const CMKSemanticModule *module, CMKASTNodeId node
)
{
    if (!module || !module->nodes || node >= module->node_count)
        return CCFALSE;
    return module->nodes[node].noreturn;
}

const CMKFunctionInfo *cmk_semantic_find_function(
    const CMKSemanticModule *module, const char *name, CCUINT32 len
)
{
    if (!module || !name)
        return NULL;
    CMKFunctionInfo **functions =
        (CMKFunctionInfo **)module->functions.array;
    CCUINT32 count =
        module->functions.size / (CCUINT32)sizeof(CMKFunctionInfo *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (functions[i]->name.len == len &&
            memcmp(functions[i]->name.data, name, len) == 0)
            return functions[i];
    }
    return NULL;
}

const CMKTargetInfo *cmk_semantic_find_target(
    const CMKSemanticModule *module, const char *name, CCUINT32 len
)
{
    return sem_find_target_name(module, name, len);
}

const CMKTargetInfo *cmk_semantic_find_target_by_decl(
    const CMKSemanticModule *module, CMKASTNodeId decl
)
{
    if (!module)
        return NULL;
    CMKTargetInfo **targets = (CMKTargetInfo **)module->targets.array;
    CCUINT32 count =
        module->targets.size / (CCUINT32)sizeof(CMKTargetInfo *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (targets[i]->decl == decl)
            return targets[i];
    }
    return NULL;
}

CCUINT32 cmk_semantic_function_count(
    const CMKSemanticModule *module
)
{
    if (!module)
        return 0;
    return module->functions.size /
        (CCUINT32)sizeof(CMKFunctionInfo *);
}

const CMKFunctionInfo *cmk_semantic_function_at(
    const CMKSemanticModule *module, CCUINT32 index
)
{
    if (!module || index >= cmk_semantic_function_count(module))
        return NULL;
    CMKFunctionInfo **functions =
        (CMKFunctionInfo **)module->functions.array;
    return functions[index];
}

CCUINT32 cmk_semantic_target_count(const CMKSemanticModule *module)
{
    if (!module)
        return 0;
    return module->targets.size /
        (CCUINT32)sizeof(CMKTargetInfo *);
}

const CMKTargetInfo *cmk_semantic_target_at(
    const CMKSemanticModule *module, CCUINT32 index
)
{
    if (!module || index >= cmk_semantic_target_count(module))
        return NULL;
    CMKTargetInfo **targets = (CMKTargetInfo **)module->targets.array;
    return targets[index];
}
