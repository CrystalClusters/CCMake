/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK Configure 求值运行时（M4）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_runtime.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CMK_RT_COMMENT CC_DBG_REL_TAG(".cmk 运行时", NULL)

#define CMK_INT64_MAX_ ((CCINT64)0x7FFFFFFFFFFFFFFF)
#define CMK_INT64_MIN_ (-(CMK_INT64_MAX_) - 1)

typedef struct cmk_env_var {
    CMKString name;
    CMKValue value;
    CCBOOL is_const;
} CMKEnvVar;

typedef struct cmk_env {
    struct cmk_env *parent;
    CCDyn vars;   /* CMKEnvVar[] */
} CMKEnv;

typedef enum cmk_exec_kind {
    CMK_EXEC_NORMAL = 0,
    CMK_EXEC_RETURN,
    CMK_EXEC_BREAK,
    CMK_EXEC_CONTINUE,
    CMK_EXEC_ERROR
} CMKExecKind;

typedef struct cmk_exec_result {
    CMKExecKind kind;
    CMKValue value;
} CMKExecResult;

struct cmk_runtime {
    CMKSemanticModule *semantic;
    CMKConfig *config;
    CMKBuiltinRegistry *builtins;
    CMKDiagnostic *diagnostic;
    CMKEnv *module_env;
    CCDyn targets;      /* CMKTargetDraft[] */
    CMKRuntimeModuleResolver module_resolver;
    void *resolver_user;
    const char *work_dir;
    CCBOOL globals_initialized;
    CCBOOL configured;
    CCBOOL failed;
};

/* ============================================================================
 * 诊断与执行结果
 * ========================================================================== */

static CCBOOL rt_error(
    CMKRuntime *runtime, CMKASTNodeId node, const char *fmt, ...
)
{
    if (!runtime || !runtime->diagnostic)
        return CCFALSE;
    runtime->failed = CCTRUE;
    if (cmk_diagnostic_has(runtime->diagnostic))
        return CCFALSE;

    const CMKASTNode *ast_node = NULL;
    const char *file = runtime->semantic ? runtime->semantic->path : "(.cmk)";
    CCUINT32 line = 0;
    CCUINT32 col = 0;
    if (runtime->semantic && runtime->semantic->ast)
    {
        ast_node = cmk_ast_node(runtime->semantic->ast, node);
        if (ast_node)
        {
            file = cmk_ast_file(runtime->semantic->ast);
            line = ast_node->span.line;
            col = ast_node->span.col;
        }
    }

    va_list ap;
    va_start(ap, fmt);
    (void)cmk_diagnostic_vset(
        runtime->diagnostic, CMK_DIAG_ERROR, file, line, col, fmt, ap
    );
    va_end(ap);
    return CCFALSE;
}

static void exec_result_init(CMKExecResult *result)
{
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    result->kind = CMK_EXEC_NORMAL;
    cmk_value_init(&result->value);
}

static void exec_result_clear(CMKExecResult *result)
{
    if (!result)
        return;
    cmk_value_clear(&result->value);
    result->kind = CMK_EXEC_NORMAL;
}

static CMKASTNodeId rt_child(
    const CMKRuntime *runtime, CMKASTNodeId parent, CCUINT32 index
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, parent);
    if (!node)
        return CMK_AST_NODE_NIL;
    CMKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < index && child != CMK_AST_NODE_NIL; i++)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;
    }
    return child;
}

void cmk_runtime_mark_fatal(CMKRuntime *runtime)
{
    if (runtime)
        runtime->failed = CCTRUE;
}

CCBOOL cmk_runtime_failed(const CMKRuntime *runtime)
{
    return (runtime && runtime->failed) ? CCTRUE : CCFALSE;
}

/* ============================================================================
 * 环境
 * ========================================================================== */

static CMKEnv *env_create(CMKEnv *parent)
{
    CMKEnv *env = ccalloc(NULL, sizeof(CMKEnv), CMK_RT_COMMENT);
    if (!env)
        return NULL;
    memset(env, 0, sizeof(*env));
    env->parent = parent;
    return env;
}

static void env_destroy(CMKEnv *env)
{
    if (!env)
        return;
    CMKEnvVar *vars = (CMKEnvVar *)env->vars.array;
    CCUINT32 count = env->vars.size / (CCUINT32)sizeof(CMKEnvVar);
    for (CCUINT32 i = 0; i < count; i++)
    {
        cmk_string_clear(&vars[i].name);
        cmk_value_clear(&vars[i].value);
    }
    if (env->vars.array)
        ccalloc(env->vars.array, 0, NULL);
    ccalloc(env, 0, NULL);
}

static CMKEnvVar *env_slot(
    CMKEnv *env, const char *name, CCUINT32 len
)
{
    for (CMKEnv *current = env; current; current = current->parent)
    {
        CMKEnvVar *vars = (CMKEnvVar *)current->vars.array;
        CCUINT32 count =
            current->vars.size / (CCUINT32)sizeof(CMKEnvVar);
        for (CCUINT32 i = 0; i < count; i++)
        {
            CMKEnvVar *var = &vars[i];
            if (var->name.len == len &&
                (len == 0 || memcmp(var->name.data, name, len) == 0))
                return var;
        }
    }
    return NULL;
}

static CCBOOL env_define(
    CMKRuntime *runtime, CMKEnv *env,
    const char *name, CCUINT32 len,
    const CMKValue *value, CCBOOL is_const, CMKASTNodeId node
)
{
    if (!env || !name || !value)
        return rt_error(runtime, node, "内部错误：环境定义参数无效");

    CMKEnvVar *vars = (CMKEnvVar *)env->vars.array;
    CCUINT32 count = env->vars.size / (CCUINT32)sizeof(CMKEnvVar);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (vars[i].name.len == len &&
            (len == 0 || memcmp(vars[i].name.data, name, len) == 0))
            return rt_error(runtime, node, "重复的变量：%.*s", (int)len, name);
    }

    CMKEnvVar var;
    memset(&var, 0, sizeof(var));
    if (!cmk_string_set(&var.name, name, len))
        return rt_error(runtime, node, "内存分配失败");
    if (!cmk_value_copy(&var.value, value))
    {
        cmk_string_clear(&var.name);
        return rt_error(runtime, node, "值复制失败");
    }
    var.is_const = is_const;
    if (cc_dyn_core_push(
            &env->vars, (CCUINT32)sizeof(CMKEnvVar),
            (CCINT32)sizeof(CMKEnvVar), &var
        ) != 0)
    {
        cmk_string_clear(&var.name);
        cmk_value_clear(&var.value);
        return rt_error(runtime, node, "内存分配失败");
    }
    return CCTRUE;
}

/* ============================================================================
 * 值类型与算术
 * ========================================================================== */

static CMKType value_type(const CMKValue *value)
{
    if (!value)
        return cmk_type_scalar(CMK_VALUE_VOID);
    if (value->type == CMK_VALUE_LIST)
    {
        CMKValueType element = value->as.list
            ? value->as.list->element_type : CMK_VALUE_VOID;
        return cmk_type_list(element);
    }
    return cmk_type_scalar(value->type);
}

static CCBOOL rt_expect_value(
    CMKRuntime *runtime, CMKASTNodeId node,
    const CMKValue *value, const CMKType *expected
)
{
    CMKType actual = value_type(value);
    if (cmk_type_equal(&actual, expected))
        return CCTRUE;
    char actual_buf[64];
    char expected_buf[64];
    cmk_type_format(&actual, actual_buf, sizeof(actual_buf));
    cmk_type_format(expected, expected_buf, sizeof(expected_buf));
    return rt_error(
        runtime, node, "运行时类型不匹配：需要 %s，实际 %s",
        expected_buf, actual_buf
    );
}

static CCBOOL int_add(CCINT64 a, CCINT64 b, CCINT64 *out)
{
    if ((b > 0 && a > CMK_INT64_MAX_ - b) ||
        (b < 0 && a < CMK_INT64_MIN_ - b))
        return CCFALSE;
    *out = a + b;
    return CCTRUE;
}

static CCBOOL int_sub(CCINT64 a, CCINT64 b, CCINT64 *out)
{
    if ((b > 0 && a < CMK_INT64_MIN_ + b) ||
        (b < 0 && a > CMK_INT64_MAX_ + b))
        return CCFALSE;
    *out = a - b;
    return CCTRUE;
}

static CCBOOL int_mul(CCINT64 a, CCINT64 b, CCINT64 *out)
{
    if (a == 0 || b == 0)
    {
        *out = 0;
        return CCTRUE;
    }
    if (a == CMK_INT64_MIN_ && b == -1)
        return CCFALSE;
    if (b == CMK_INT64_MIN_ && a == -1)
        return CCFALSE;

    if ((a > 0 && b > 0 && a > CMK_INT64_MAX_ / b) ||
        (a > 0 && b < 0 && b < CMK_INT64_MIN_ / a) ||
        (a < 0 && b > 0 && a < CMK_INT64_MIN_ / b) ||
        (a < 0 && b < 0 && a < CMK_INT64_MAX_ / b))
        return CCFALSE;
    *out = a * b;
    return CCTRUE;
}

static int string_compare(const CMKString *a, const CMKString *b)
{
    CCUINT32 min = a->len < b->len ? a->len : b->len;
    if (min > 0)
    {
        int cmp = memcmp(a->data, b->data, min);
        if (cmp != 0)
            return cmp;
    }
    if (a->len < b->len)
        return -1;
    if (a->len > b->len)
        return 1;
    return 0;
}

/* ============================================================================
 * 表达式求值
 * ========================================================================== */

static CCBOOL rt_eval_expr(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
);

static CCBOOL rt_eval_config(
    CMKRuntime *runtime, CMKASTNodeId id, CMKValue *out
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_CONFIG_REF)
        return rt_error(runtime, id, "内部错误：配置引用节点");

    CCUINT32 path_len = 0;
    const char *path = cmk_ast_string(
        runtime->semantic->ast, node->as.config_ref.path, &path_len
    );
    if (!path)
        return rt_error(runtime, id, "配置路径无效");

    CMKValueType expected_element = CMK_VALUE_VOID;
    const CMKType *type = cmk_semantic_node_type(runtime->semantic, id);
    if (type && type->kind == CMK_VALUE_LIST)
        expected_element = type->element;

    CMKConfigResult result = cmk_config_value(
        runtime->config, path, expected_element, out
    );
    switch (result)
    {
    case CMK_CONFIG_OK:
        break;

    case CMK_CONFIG_MISSING:
        return rt_error(runtime, id, "配置字段不存在：%s", path);

    case CMK_CONFIG_MIXED:
        return rt_error(runtime, id, "配置数组元素类型不一致：%s", path);

    case CMK_CONFIG_NOMEM:
        return rt_error(runtime, id, "配置查询内存分配失败");

    default:
        return rt_error(runtime, id, "配置字段类型不受支持：%s", path);
    }

    if (type && !rt_expect_value(runtime, id, out, type))
    {
        cmk_value_clear(out);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_eval_list(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    const CMKType *type = cmk_semantic_node_type(runtime->semantic, id);
    if (!type || type->kind != CMK_VALUE_LIST ||
        type->element == CMK_VALUE_VOID)
        return rt_error(runtime, id, "空列表或列表元素类型未解析");

    if (!cmk_value_set_list(out, type->element))
        return rt_error(runtime, id, "列表分配失败");

    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "列表节点缺失");

    for (CMKASTNodeId child = node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        CMKValue item;
        cmk_value_init(&item);
        if (!rt_eval_expr(runtime, child, env, &item))
        {
            cmk_value_clear(&item);
            cmk_value_clear(out);
            return CCFALSE;
        }
        if (!cmk_list_append(out->as.list, &item))
        {
            cmk_value_clear(&item);
            cmk_value_clear(out);
            return rt_error(runtime, id, "列表追加失败");
        }
        cmk_value_clear(&item);

        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        if (!child_node)
            break;
        child = child_node->next_sibling;
    }
    return CCTRUE;
}

static CCBOOL rt_eval_ident(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    const CMKTargetInfo *target =
        cmk_semantic_node_target_ref(runtime->semantic, id);
    if (target)
    {
        return cmk_value_set_target_ref(
            out, target->module->path,
            (CCUINT32)strlen(target->module->path),
            target->name.data, target->name.len
        ) ? CCTRUE : rt_error(runtime, id, "目标引用复制失败");
    }

    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_IDENT)
        return rt_error(runtime, id, "内部错误：标识符节点");

    CCUINT32 name_len = 0;
    const char *name = cmk_ast_string(
        runtime->semantic->ast, node->as.ident.name, &name_len
    );
    if (!name)
        return rt_error(runtime, id, "变量名无效");

    CMKEnvVar *slot = env_slot(env, name, name_len);
    if (!slot)
        return rt_error(runtime, id, "未定义的变量：%.*s", (int)name_len, name);
    if (!cmk_value_copy(out, &slot->value))
        return rt_error(runtime, id, "值复制失败");
    return CCTRUE;
}

static CCBOOL rt_eval_unary(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_UNARY)
        return rt_error(runtime, id, "内部错误：一元表达式节点");

    CMKASTNodeId operand =
        node->first_child;
    CMKValue value;
    cmk_value_init(&value);
    if (!rt_eval_expr(runtime, operand, env, &value))
    {
        cmk_value_clear(&value);
        return CCFALSE;
    }

    CCBOOL ok = CCFALSE;
    switch (node->as.unary.op)
    {
    case CMK_UNARY_NOT:
        if (value.type != CMK_VALUE_BOOL)
            ok = rt_error(runtime, id, "! 需要 bool");
        else
            ok = cmk_value_set_bool(out, value.as.boolean ? CCFALSE : CCTRUE);
        break;

    case CMK_UNARY_NEG:
        if (value.type == CMK_VALUE_INT)
        {
            if (value.as.integer == CMK_INT64_MIN_)
                ok = rt_error(runtime, id, "整数取负溢出");
            else
                ok = cmk_value_set_int(out, -value.as.integer);
        }
        else if (value.type == CMK_VALUE_FLOAT)
            ok = cmk_value_set_float(out, -value.as.floating);
        else
            ok = rt_error(runtime, id, "- 需要数值");
        break;

    case CMK_UNARY_POS:
        if (value.type == CMK_VALUE_INT || value.type == CMK_VALUE_FLOAT)
            ok = cmk_value_copy(out, &value);
        else
            ok = rt_error(runtime, id, "+ 需要数值");
        break;

    default:
        ok = rt_error(runtime, id, "未知一元运算符");
        break;
    }

    cmk_value_clear(&value);
    return ok;
}

static CCBOOL rt_eval_binary(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_BINARY)
        return rt_error(runtime, id, "内部错误：二元表达式节点");

    CMKASTNodeId left_id = node->first_child;
    const CMKASTNode *left_node =
        cmk_ast_node(runtime->semantic->ast, left_id);
    if (!left_node)
        return rt_error(runtime, id, "二元表达式左操作数缺失");
    CMKASTNodeId right_id = left_node->next_sibling;
    CMKBinaryOp op = node->as.binary.op;

    if (op == CMK_BINARY_AND || op == CMK_BINARY_OR)
    {
        CMKValue left;
        cmk_value_init(&left);
        if (!rt_eval_expr(runtime, left_id, env, &left))
        {
            cmk_value_clear(&left);
            return CCFALSE;
        }
        if (left.type != CMK_VALUE_BOOL)
        {
            cmk_value_clear(&left);
            return rt_error(runtime, id, "逻辑运算需要 bool");
        }
        if (op == CMK_BINARY_AND && !left.as.boolean)
        {
            cmk_value_clear(&left);
            return cmk_value_set_bool(out, CCFALSE);
        }
        if (op == CMK_BINARY_OR && left.as.boolean)
        {
            cmk_value_clear(&left);
            return cmk_value_set_bool(out, CCTRUE);
        }
        cmk_value_clear(&left);

        CMKValue right;
        cmk_value_init(&right);
        if (!rt_eval_expr(runtime, right_id, env, &right))
        {
            cmk_value_clear(&right);
            return CCFALSE;
        }
        if (right.type != CMK_VALUE_BOOL)
        {
            cmk_value_clear(&right);
            return rt_error(runtime, id, "逻辑运算需要 bool");
        }
        CCBOOL result = right.as.boolean;
        cmk_value_clear(&right);
        return cmk_value_set_bool(out, result);
    }

    CMKValue left;
    CMKValue right;
    cmk_value_init(&left);
    cmk_value_init(&right);
    CCBOOL left_ok = rt_eval_expr(runtime, left_id, env, &left);
    CCBOOL right_ok = left_ok
        ? rt_eval_expr(runtime, right_id, env, &right) : CCFALSE;
    if (!left_ok || !right_ok)
    {
        cmk_value_clear(&left);
        cmk_value_clear(&right);
        return CCFALSE;
    }

    CCBOOL ok = CCFALSE;
    switch (op)
    {
    case CMK_BINARY_EQ:
    case CMK_BINARY_NE:
        if (left.type != right.type)
            ok = rt_error(runtime, id, "比较两侧类型不一致");
        else
        {
            CCBOOL equal = cmk_value_equal(&left, &right);
            if (op == CMK_BINARY_NE)
                equal = equal ? CCFALSE : CCTRUE;
            ok = cmk_value_set_bool(out, equal);
        }
        break;

    case CMK_BINARY_LT:
    case CMK_BINARY_LE:
    case CMK_BINARY_GT:
    case CMK_BINARY_GE:
    {
        int cmp = 0;
        if (left.type == CMK_VALUE_INT && right.type == CMK_VALUE_INT)
            cmp = left.as.integer < right.as.integer ? -1
                : (left.as.integer > right.as.integer ? 1 : 0);
        else if (left.type == CMK_VALUE_FLOAT &&
                 right.type == CMK_VALUE_FLOAT)
            cmp = left.as.floating < right.as.floating ? -1
                : (left.as.floating > right.as.floating ? 1 : 0);
        else if (left.type == CMK_VALUE_STRING &&
                 right.type == CMK_VALUE_STRING)
            cmp = string_compare(&left.as.string, &right.as.string);
        else
        {
            ok = rt_error(runtime, id, "比较类型不受支持");
            break;
        }
        CCBOOL result = CCFALSE;
        switch (op)
        {
        case CMK_BINARY_LT:
            result = cmp < 0;
            break;
        case CMK_BINARY_LE:
            result = cmp <= 0;
            break;
        case CMK_BINARY_GT:
            result = cmp > 0;
            break;
        default:
            result = cmp >= 0;
            break;
        }
        ok = cmk_value_set_bool(out, result);
        break;
    }

    case CMK_BINARY_ADD:
        if (left.type == CMK_VALUE_INT && right.type == CMK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_add(left.as.integer, right.as.integer, &value))
                ok = rt_error(runtime, id, "整数加法溢出");
            else
                ok = cmk_value_set_int(out, value);
        }
        else if (left.type == CMK_VALUE_FLOAT &&
                 right.type == CMK_VALUE_FLOAT)
            ok = cmk_value_set_float(out, left.as.floating + right.as.floating);
        else if (left.type == CMK_VALUE_STRING &&
                 right.type == CMK_VALUE_STRING)
        {
            CMKString text;
            cmk_string_init(&text);
            if (!cmk_string_append(
                    &text, left.as.string.data, left.as.string.len
                ) ||
                !cmk_string_append(
                    &text, right.as.string.data, right.as.string.len
                ))
                ok = rt_error(runtime, id, "字符串拼接内存分配失败");
            else
                ok = cmk_value_set_string(out, text.data, text.len);
            cmk_string_clear(&text);
        }
        else if (left.type == CMK_VALUE_LIST && right.type == CMK_VALUE_LIST)
        {
            CMKValueType element = left.as.list->element_type;
            if (element == CMK_VALUE_VOID)
                element = right.as.list->element_type;
            if (element == CMK_VALUE_VOID)
            {
                ok = rt_error(runtime, id, "空列表类型未解析");
                break;
            }
            if (!cmk_value_set_list(out, element))
            {
                ok = rt_error(runtime, id, "列表分配失败");
                break;
            }
            ok = CCTRUE;
            CCUINT32 left_count = cmk_list_count(left.as.list);
            for (CCUINT32 i = 0; i < left_count; i++)
            {
                if (!cmk_list_append(out->as.list, cmk_list_at(left.as.list, i)))
                {
                    ok = rt_error(runtime, id, "列表拼接失败");
                    break;
                }
            }
            if (!ok)
                break;
            CCUINT32 right_count = cmk_list_count(right.as.list);
            for (CCUINT32 i = 0; i < right_count; i++)
            {
                if (!cmk_list_append(
                        out->as.list, cmk_list_at(right.as.list, i)
                    ))
                {
                    ok = rt_error(runtime, id, "列表拼接失败");
                    break;
                }
            }
        }
        else
            ok = rt_error(runtime, id, "加法类型不受支持");
        break;

    case CMK_BINARY_SUB:
        if (left.type == CMK_VALUE_INT && right.type == CMK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_sub(left.as.integer, right.as.integer, &value))
                ok = rt_error(runtime, id, "整数减法溢出");
            else
                ok = cmk_value_set_int(out, value);
        }
        else if (left.type == CMK_VALUE_FLOAT &&
                 right.type == CMK_VALUE_FLOAT)
            ok = cmk_value_set_float(out, left.as.floating - right.as.floating);
        else
            ok = rt_error(runtime, id, "减法类型不受支持");
        break;

    case CMK_BINARY_MUL:
        if (left.type == CMK_VALUE_INT && right.type == CMK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_mul(left.as.integer, right.as.integer, &value))
                ok = rt_error(runtime, id, "整数乘法溢出");
            else
                ok = cmk_value_set_int(out, value);
        }
        else if (left.type == CMK_VALUE_FLOAT &&
                 right.type == CMK_VALUE_FLOAT)
            ok = cmk_value_set_float(out, left.as.floating * right.as.floating);
        else
            ok = rt_error(runtime, id, "乘法类型不受支持");
        break;

    case CMK_BINARY_DIV:
        if (left.type == CMK_VALUE_INT && right.type == CMK_VALUE_INT)
        {
            if (right.as.integer == 0)
                ok = rt_error(runtime, id, "整数除数为零");
            else if (left.as.integer == CMK_INT64_MIN_ &&
                     right.as.integer == -1)
                ok = rt_error(runtime, id, "整数除法溢出");
            else
                ok = cmk_value_set_int(
                    out, left.as.integer / right.as.integer
                );
        }
        else if (left.type == CMK_VALUE_FLOAT &&
                 right.type == CMK_VALUE_FLOAT)
        {
            if (right.as.floating == 0.0)
                ok = rt_error(runtime, id, "浮点除数为零");
            else
                ok = cmk_value_set_float(
                    out, left.as.floating / right.as.floating
                );
        }
        else
            ok = rt_error(runtime, id, "除法类型不受支持");
        break;

    case CMK_BINARY_MOD:
        if (left.type != CMK_VALUE_INT || right.type != CMK_VALUE_INT)
            ok = rt_error(runtime, id, "取模需要 int");
        else if (right.as.integer == 0)
            ok = rt_error(runtime, id, "取模除数为零");
        else if (left.as.integer == CMK_INT64_MIN_ &&
                 right.as.integer == -1)
            ok = rt_error(runtime, id, "取模溢出");
        else
            ok = cmk_value_set_int(
                out, left.as.integer % right.as.integer
            );
        break;

    default:
        ok = rt_error(runtime, id, "未知二元运算符");
        break;
    }

    cmk_value_clear(&left);
    cmk_value_clear(&right);
    return ok;
}

static CCBOOL rt_eval_call_user(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    const CMKFunctionInfo *function, CMKValue *out
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_CALL)
        return rt_error(runtime, id, "内部错误：调用节点");

    CCUINT32 argc = 0;
    for (CMKASTNodeId child = node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        argc++;
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        if (!child_node)
            break;
        child = child_node->next_sibling;
    }

    CMKValue *args = NULL;
    if (argc > 0)
    {
        args = ccalloc(
            NULL, (CCUINT64)argc * sizeof(CMKValue), CMK_RT_COMMENT
        );
        if (!args)
            return rt_error(runtime, id, "内存分配失败");
        for (CCUINT32 i = 0; i < argc; i++)
            cmk_value_init(&args[i]);
    }

    CMKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < argc; i++)
    {
        if (!rt_eval_expr(runtime, child, env, &args[i]))
        {
            for (CCUINT32 j = 0; j < argc; j++)
                cmk_value_clear(&args[j]);
            if (args)
                ccalloc(args, 0, NULL);
            return CCFALSE;
        }
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;
    }

    CCBOOL ok = cmk_runtime_call_function(
        runtime, function, args, argc, out
    );
    for (CCUINT32 i = 0; i < argc; i++)
        cmk_value_clear(&args[i]);
    if (args)
        ccalloc(args, 0, NULL);
    return ok;
}

static CCBOOL rt_eval_call(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    const CMKFunctionInfo *function =
        cmk_semantic_node_function(runtime->semantic, id);
    if (function)
        return rt_eval_call_user(runtime, id, env, function, out);

    const CMKBuiltinSpec *spec =
        cmk_semantic_node_builtin(runtime->semantic, id);
    if (!spec)
        return rt_error(runtime, id, "调用未完成语义绑定");

    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "调用节点缺失");

    CCUINT32 argc = 0;
    for (CMKASTNodeId child = node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        argc++;
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        if (!child_node)
            break;
        child = child_node->next_sibling;
    }

    CMKValue *args = NULL;
    if (argc > 0)
    {
        args = ccalloc(
            NULL, (CCUINT64)argc * sizeof(CMKValue), CMK_RT_COMMENT
        );
        if (!args)
            return rt_error(runtime, id, "内存分配失败");
        for (CCUINT32 i = 0; i < argc; i++)
            cmk_value_init(&args[i]);
    }

    CMKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < argc; i++)
    {
        if (!rt_eval_expr(runtime, child, env, &args[i]))
        {
            for (CCUINT32 j = 0; j < argc; j++)
                cmk_value_clear(&args[j]);
            if (args)
                ccalloc(args, 0, NULL);
            return CCFALSE;
        }
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;
    }

    CMKValue result;
    cmk_value_init(&result);
    CMKBuiltinCall call;
    memset(&call, 0, sizeof(call));
    call.runtime = runtime;
    call.args = argc > 0 ? args : NULL;
    call.argc = argc;
    call.out = &result;
    call.diagnostic = runtime->diagnostic;
    call.file = cmk_ast_file(runtime->semantic->ast);
    call.line = node->span.line;
    call.col = node->span.col;

    CCBOOL ok = spec->fn(&call);
    if (!ok)
    {
        cmk_value_clear(&result);
        for (CCUINT32 i = 0; i < argc; i++)
            cmk_value_clear(&args[i]);
        if (args)
            ccalloc(args, 0, NULL);
        if (runtime->failed)
            return CCFALSE;
        return rt_error(runtime, id, "内建调用失败：%s", spec->name);
    }

    CMKType actual = value_type(&result);
    if (!cmk_type_equal(&actual, &spec->return_type))
    {
        cmk_value_clear(&result);
        for (CCUINT32 i = 0; i < argc; i++)
            cmk_value_clear(&args[i]);
        if (args)
            ccalloc(args, 0, NULL);
        return rt_error(runtime, id, "内建返回值类型错误：%s", spec->name);
    }

    cmk_value_copy(out, &result);
    cmk_value_clear(&result);
    for (CCUINT32 i = 0; i < argc; i++)
        cmk_value_clear(&args[i]);
    if (args)
        ccalloc(args, 0, NULL);
    return CCTRUE;
}

static CCBOOL rt_eval_index(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_INDEX)
        return rt_error(runtime, id, "内部错误：索引节点");

    CMKASTNodeId base_id = node->first_child;
    const CMKASTNode *base_node =
        cmk_ast_node(runtime->semantic->ast, base_id);
    if (!base_node)
        return rt_error(runtime, id, "索引基缺失");
    CMKASTNodeId index_id = base_node->next_sibling;

    CMKValue base;
    CMKValue index;
    cmk_value_init(&base);
    cmk_value_init(&index);
    if (!rt_eval_expr(runtime, base_id, env, &base) ||
        !rt_eval_expr(runtime, index_id, env, &index))
    {
        cmk_value_clear(&base);
        cmk_value_clear(&index);
        return CCFALSE;
    }
    if (base.type != CMK_VALUE_LIST || index.type != CMK_VALUE_INT)
    {
        cmk_value_clear(&base);
        cmk_value_clear(&index);
        return rt_error(runtime, id, "索引需要 list[int]");
    }
    CCINT64 at = index.as.integer;
    if (at < 0 || (CCUINT64)at >= cmk_list_count(base.as.list))
    {
        cmk_value_clear(&base);
        cmk_value_clear(&index);
        return rt_error(runtime, id, "列表索引越界");
    }
    CCBOOL ok = cmk_value_copy(
        out, cmk_list_at(base.as.list, (CCUINT32)at)
    );
    cmk_value_clear(&base);
    cmk_value_clear(&index);
    return ok ? CCTRUE : rt_error(runtime, id, "索引值复制失败");
}

static CCBOOL rt_eval_expr(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    if (!runtime || !runtime->semantic || runtime->failed)
        return CCFALSE;

    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "表达式节点缺失");

    switch (node->kind)
    {
    case CMK_AST_LITERAL:
        switch (node->as.literal.kind)
        {
        case CMK_LITERAL_INT:
            return cmk_value_set_int(out, node->as.literal.value.integer);

        case CMK_LITERAL_FLOAT:
            return cmk_value_set_float(out, node->as.literal.value.floating);

        case CMK_LITERAL_STRING:
        {
            CCUINT32 len = 0;
            const char *text = cmk_ast_string(
                runtime->semantic->ast,
                node->as.literal.value.string, &len
            );
            return text
                ? cmk_value_set_string(out, text, len)
                : rt_error(runtime, id, "字符串值无效");
        }

        case CMK_LITERAL_BOOL:
            return cmk_value_set_bool(out, node->as.literal.value.boolean);

        default:
            return rt_error(runtime, id, "未知字面量");
        }

    case CMK_AST_IDENT:
        return rt_eval_ident(runtime, id, env, out);

    case CMK_AST_CONFIG_REF:
        return rt_eval_config(runtime, id, out);

    case CMK_AST_LIST:
        return rt_eval_list(runtime, id, env, out);

    case CMK_AST_UNARY:
        return rt_eval_unary(runtime, id, env, out);

    case CMK_AST_BINARY:
        return rt_eval_binary(runtime, id, env, out);

    case CMK_AST_CALL:
        return rt_eval_call(runtime, id, env, out);

    case CMK_AST_INDEX:
        return rt_eval_index(runtime, id, env, out);

    case CMK_AST_MEMBER:
    {
        const CMKTargetInfo *target =
            cmk_semantic_node_target_ref(runtime->semantic, id);
        if (!target)
            return rt_error(runtime, id, "目标引用未绑定");
        return cmk_value_set_target_ref(
            out, target->module->path,
            (CCUINT32)strlen(target->module->path),
            target->name.data, target->name.len
        ) ? CCTRUE : rt_error(runtime, id, "目标引用复制失败");
    }

    default:
        return rt_error(
            runtime, id, "不支持的表达式节点：%s",
            cmk_ast_kind_name(node->kind)
        );
    }
}

/* ============================================================================
 * 语句求值
 * ========================================================================== */

static CCBOOL rt_eval_stmt(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CMKExecResult *result
);

static CCBOOL rt_eval_block(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CCBOOL create_scope, CMKExecResult *result
)
{
    CMKEnv *block_env = env;
    if (create_scope)
    {
        block_env = env_create(env);
        if (!block_env)
            return rt_error(runtime, id, "内存分配失败");
    }

    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
    {
        if (create_scope)
            env_destroy(block_env);
        return rt_error(runtime, id, "代码块节点缺失");
    }

    exec_result_init(result);
    for (CMKASTNodeId child = node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        CMKExecResult item;
        if (!rt_eval_stmt(runtime, child, block_env, &item))
        {
            if (create_scope)
                env_destroy(block_env);
            return CCFALSE;
        }
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        CMKASTNodeId next =
            child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;

        if (item.kind != CMK_EXEC_NORMAL)
        {
            *result = item;
            cmk_value_init(&item.value);
            if (create_scope)
                env_destroy(block_env);
            return CCTRUE;
        }
        exec_result_clear(&item);
        child = next;
    }

    if (create_scope)
        env_destroy(block_env);
    return CCTRUE;
}

static CCBOOL rt_eval_var_decl(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_VAR_DECL)
        return rt_error(runtime, id, "内部错误：变量声明");

    CMKASTNodeId init_id = CMK_AST_NODE_NIL;
    CMKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < 1 && child != CMK_AST_NODE_NIL; i++)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;
    }
    init_id = child;

    CMKValue value;
    cmk_value_init(&value);
    if (!rt_eval_expr(runtime, init_id, env, &value))
    {
        cmk_value_clear(&value);
        return CCFALSE;
    }

    CCUINT32 name_len = 0;
    const char *name = cmk_ast_string(
        runtime->semantic->ast, node->as.var_decl.name, &name_len
    );
    CCBOOL ok = name && env_define(
        runtime, env, name, name_len, &value,
        node->as.var_decl.is_const, id
    );
    if (!ok && !name)
        ok = rt_error(runtime, id, "变量名无效");
    if (!ok && name && !cmk_diagnostic_has(runtime->diagnostic))
        ok = rt_error(runtime, id, "变量 %s 初始化失败", name);
    cmk_value_clear(&value);
    return ok;
}

static CCBOOL rt_eval_assign(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_ASSIGN)
        return rt_error(runtime, id, "内部错误：赋值语句");

    CMKASTNodeId target_id = node->first_child;
    const CMKASTNode *target =
        cmk_ast_node(runtime->semantic->ast, target_id);
    if (!target)
        return rt_error(runtime, id, "赋值目标缺失");
    CMKASTNodeId value_id = target->next_sibling;

    CMKValue value;
    cmk_value_init(&value);
    if (!rt_eval_expr(runtime, value_id, env, &value))
    {
        cmk_value_clear(&value);
        return CCFALSE;
    }

    if (target->kind == CMK_AST_DISCARD)
    {
        cmk_value_clear(&value);
        return CCTRUE;
    }
    if (target->kind != CMK_AST_IDENT)
    {
        cmk_value_clear(&value);
        return rt_error(runtime, id, "赋值目标非法");
    }

    CCUINT32 name_len = 0;
    const char *name = cmk_ast_string(
        runtime->semantic->ast, target->as.ident.name, &name_len
    );
    if (!name)
    {
        cmk_value_clear(&value);
        return rt_error(runtime, id, "变量名无效");
    }
    CMKEnvVar *slot = env_slot(env, name, name_len);
    if (!slot)
    {
        cmk_value_clear(&value);
        return rt_error(runtime, id, "未定义的变量：%.*s", (int)name_len, name);
    }
    if (slot->is_const)
    {
        cmk_value_clear(&value);
        return rt_error(
            runtime, id, "const 变量不能重新赋值：%.*s",
            (int)name_len, name
        );
    }

    CCBOOL ok = CCFALSE;
    if (node->as.assign.op == CMK_ASSIGN_SET)
    {
        ok = cmk_value_copy(&slot->value, &value);
        if (!ok)
            ok = rt_error(runtime, id, "值复制失败");
    }
    else if (slot->value.type == CMK_VALUE_LIST)
    {
        if (value.type != slot->value.as.list->element_type)
            ok = rt_error(runtime, id, "list += 元素类型不匹配");
        else
            ok = cmk_list_append(slot->value.as.list, &value)
                ? CCTRUE : rt_error(runtime, id, "list += 追加失败");
    }
    else if (slot->value.type == CMK_VALUE_INT && value.type == CMK_VALUE_INT)
    {
        CCINT64 sum = 0;
        if (!int_add(slot->value.as.integer, value.as.integer, &sum))
            ok = rt_error(runtime, id, "整数 += 溢出");
        else
            ok = cmk_value_set_int(&slot->value, sum);
    }
    else if (slot->value.type == CMK_VALUE_FLOAT &&
             value.type == CMK_VALUE_FLOAT)
        ok = cmk_value_set_float(
            &slot->value, slot->value.as.floating + value.as.floating
        );
    else
        ok = rt_error(runtime, id, "+= 类型不受支持");

    cmk_value_clear(&value);
    return ok;
}

static CCBOOL rt_eval_if(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CMKExecResult *result
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_IF)
        return rt_error(runtime, id, "内部错误：if");

    CMKASTNodeId condition_id = node->first_child;
    const CMKASTNode *condition_node =
        cmk_ast_node(runtime->semantic->ast, condition_id);
    if (!condition_node)
        return rt_error(runtime, id, "if 条件缺失");
    CMKASTNodeId then_id = condition_node->next_sibling;
    const CMKASTNode *then_node =
        cmk_ast_node(runtime->semantic->ast, then_id);
    CMKASTNodeId else_id = then_node ? then_node->next_sibling : CMK_AST_NODE_NIL;

    CMKValue condition;
    cmk_value_init(&condition);
    if (!rt_eval_expr(runtime, condition_id, env, &condition))
    {
        cmk_value_clear(&condition);
        return CCFALSE;
    }
    if (condition.type != CMK_VALUE_BOOL)
    {
        cmk_value_clear(&condition);
        return rt_error(runtime, id, "if 条件不是 bool");
    }
    CCBOOL choose_then = condition.as.boolean;
    cmk_value_clear(&condition);

    if (choose_then)
        return rt_eval_stmt(runtime, then_id, env, result);
    if (else_id != CMK_AST_NODE_NIL)
        return rt_eval_stmt(runtime, else_id, env, result);
    exec_result_init(result);
    return CCTRUE;
}

static CCBOOL rt_eval_while(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CMKExecResult *result
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_WHILE)
        return rt_error(runtime, id, "内部错误：while");

    CMKASTNodeId condition_id = node->first_child;
    const CMKASTNode *condition_node =
        cmk_ast_node(runtime->semantic->ast, condition_id);
    if (!condition_node)
        return rt_error(runtime, id, "while 条件缺失");
    CMKASTNodeId body_id = condition_node->next_sibling;

    exec_result_init(result);
    for (;;)
    {
        CMKValue condition;
        cmk_value_init(&condition);
        if (!rt_eval_expr(runtime, condition_id, env, &condition))
        {
            cmk_value_clear(&condition);
            return CCFALSE;
        }
        if (condition.type != CMK_VALUE_BOOL)
        {
            cmk_value_clear(&condition);
            return rt_error(runtime, id, "while 条件不是 bool");
        }
        CCBOOL run = condition.as.boolean;
        cmk_value_clear(&condition);
        if (!run)
            return CCTRUE;

        CMKExecResult body;
        if (!rt_eval_stmt(runtime, body_id, env, &body))
            return CCFALSE;
        if (body.kind == CMK_EXEC_BREAK)
        {
            exec_result_clear(&body);
            return CCTRUE;
        }
        if (body.kind == CMK_EXEC_CONTINUE)
        {
            exec_result_clear(&body);
            continue;
        }
        if (body.kind != CMK_EXEC_NORMAL)
        {
            *result = body;
            cmk_value_init(&body.value);
            return CCTRUE;
        }
        exec_result_clear(&body);
    }
}

static CCBOOL rt_eval_for(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CMKExecResult *result
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_FOR_IN)
        return rt_error(runtime, id, "内部错误：for-in");

    CMKASTNodeId type_id = node->first_child;
    const CMKASTNode *type_node =
        cmk_ast_node(runtime->semantic->ast, type_id);
    if (!type_node)
        return rt_error(runtime, id, "for-in 类型缺失");
    CMKASTNodeId iterable_id = type_node->next_sibling;
    const CMKASTNode *iterable_node =
        cmk_ast_node(runtime->semantic->ast, iterable_id);
    CMKASTNodeId body_id =
        iterable_node ? iterable_node->next_sibling : CMK_AST_NODE_NIL;

    CMKValue iterable;
    cmk_value_init(&iterable);
    if (!rt_eval_expr(runtime, iterable_id, env, &iterable))
    {
        cmk_value_clear(&iterable);
        return CCFALSE;
    }
    if (iterable.type != CMK_VALUE_LIST)
    {
        cmk_value_clear(&iterable);
        return rt_error(runtime, id, "for-in 需要 list");
    }

    CMKEnv *loop_env = env_create(env);
    if (!loop_env)
    {
        cmk_value_clear(&iterable);
        return rt_error(runtime, id, "内存分配失败");
    }

    CCUINT32 name_len = 0;
    const char *name = cmk_ast_string(
        runtime->semantic->ast, node->as.for_in.name, &name_len
    );
    if (!name)
    {
        cmk_value_clear(&iterable);
        env_destroy(loop_env);
        return rt_error(runtime, id, "for-in 变量名无效");
    }

    exec_result_init(result);
    CCUINT32 count = cmk_list_count(iterable.as.list);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKValue *item = cmk_list_at(iterable.as.list, i);
        if (!item)
            continue;

        CMKEnvVar *slot = env_slot(loop_env, name, name_len);
        if (!slot)
        {
            if (!env_define(
                    runtime, loop_env, name, name_len, item,
                    CCFALSE, id
                ))
            {
                cmk_value_clear(&iterable);
                env_destroy(loop_env);
                return CCFALSE;
            }
        }
        else if (!cmk_value_copy(&slot->value, item))
        {
            cmk_value_clear(&iterable);
            env_destroy(loop_env);
            return rt_error(runtime, id, "循环变量赋值失败");
        }

        CMKExecResult body;
        if (!rt_eval_stmt(runtime, body_id, loop_env, &body))
        {
            cmk_value_clear(&iterable);
            env_destroy(loop_env);
            return CCFALSE;
        }
        if (body.kind == CMK_EXEC_BREAK)
        {
            exec_result_clear(&body);
            break;
        }
        if (body.kind == CMK_EXEC_CONTINUE)
        {
            exec_result_clear(&body);
            continue;
        }
        if (body.kind != CMK_EXEC_NORMAL)
        {
            *result = body;
            cmk_value_init(&body.value);
            cmk_value_clear(&iterable);
            env_destroy(loop_env);
            return CCTRUE;
        }
        exec_result_clear(&body);
    }

    cmk_value_clear(&iterable);
    env_destroy(loop_env);
    return CCTRUE;
}

static CCBOOL rt_eval_stmt(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CMKExecResult *result
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "语句节点缺失");

    exec_result_init(result);
    switch (node->kind)
    {
    case CMK_AST_BLOCK:
        exec_result_clear(result);
        return rt_eval_block(runtime, id, env, CCTRUE, result);

    case CMK_AST_VAR_DECL:
        return rt_eval_var_decl(runtime, id, env);

    case CMK_AST_ASSIGN:
        return rt_eval_assign(runtime, id, env);

    case CMK_AST_EXPR_STMT:
    {
        CMKASTNodeId expr = node->first_child;
        CMKValue value;
        cmk_value_init(&value);
        if (!rt_eval_expr(runtime, expr, env, &value))
        {
            cmk_value_clear(&value);
            return CCFALSE;
        }
        cmk_value_clear(&value);
        return CCTRUE;
    }

    case CMK_AST_IF:
    {
        exec_result_clear(result);
        return rt_eval_if(runtime, id, env, result);
    }

    case CMK_AST_WHILE:
    {
        exec_result_clear(result);
        return rt_eval_while(runtime, id, env, result);
    }

    case CMK_AST_FOR_IN:
    {
        exec_result_clear(result);
        return rt_eval_for(runtime, id, env, result);
    }

    case CMK_AST_RETURN:
    {
        result->kind = CMK_EXEC_RETURN;
        if (node->as.return_stmt.has_value)
        {
            CMKASTNodeId value_id = node->first_child;
            return rt_eval_expr(runtime, value_id, env, &result->value);
        }
        return CCTRUE;
    }

    case CMK_AST_BREAK:
        result->kind = CMK_EXEC_BREAK;
        return CCTRUE;

    case CMK_AST_CONTINUE:
        result->kind = CMK_EXEC_CONTINUE;
        return CCTRUE;

    default:
        return rt_error(
            runtime, id, "不支持的语句节点：%s",
            cmk_ast_kind_name(node->kind)
        );
    }
}

/* ============================================================================
 * 函数
 * ========================================================================== */

CCBOOL cmk_runtime_call_function(
    CMKRuntime *runtime,
    const CMKFunctionInfo *function,
    const CMKValue *args, CCUINT32 argc,
    CMKValue *out
)
{
    if (!runtime || !function || !out)
        return CCFALSE;
    if (function->module != runtime->semantic)
    {
        CMKRuntime *target = runtime->module_resolver
            ? runtime->module_resolver(runtime, function->module)
            : NULL;
        if (!target || target == runtime)
            return rt_error(
                runtime, function->decl,
                "跨模块函数调用缺少运行时：%s", function->name.data
            );
        if (!cmk_runtime_init_globals(target))
            return CCFALSE;
        return cmk_runtime_call_function(target, function, args, argc, out);
    }
    if (argc != function->param_count)
        return rt_error(
            runtime, function->decl, "函数 %s 参数数量错误",
            function->name.data
        );

    CMKEnv *function_env = env_create(runtime->module_env);
    if (!function_env)
        return rt_error(runtime, function->decl, "内存分配失败");

    const CMKASTNode *decl =
        cmk_ast_node(runtime->semantic->ast, function->decl);
    if (!decl)
    {
        env_destroy(function_env);
        return rt_error(runtime, function->decl, "函数声明缺失");
    }

    CMKASTNodeId param = decl->first_child;
    const CMKASTNode *param_type =
        cmk_ast_node(runtime->semantic->ast, param);
    param = param_type ? param_type->next_sibling : CMK_AST_NODE_NIL;

    for (CCUINT32 i = 0; i < argc; i++)
    {
        const CMKASTNode *param_node =
            cmk_ast_node(runtime->semantic->ast, param);
        if (!param_node || param_node->kind != CMK_AST_PARAM)
        {
            env_destroy(function_env);
            return rt_error(runtime, function->decl, "函数参数缺失");
        }
        if (!rt_expect_value(
                runtime, param, &args[i], &function->params[i]
            ))
        {
            env_destroy(function_env);
            return CCFALSE;
        }
        CCUINT32 name_len = 0;
        const char *name = cmk_ast_string(
            runtime->semantic->ast, param_node->as.param.name, &name_len
        );
        if (!name || !env_define(
                runtime, function_env, name, name_len,
                &args[i], CCFALSE, param
            ))
        {
            env_destroy(function_env);
            return CCFALSE;
        }
        const CMKASTNode *next =
            cmk_ast_node(runtime->semantic->ast, param_node->next_sibling);
        param = next ? param_node->next_sibling : CMK_AST_NODE_NIL;
    }

    CMKASTNodeId body = CMK_AST_NODE_NIL;
    CMKASTNodeId child = decl->first_child;
    for (CCUINT32 i = 0; i < function->param_count + 1; i++)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;
    }
    body = child;

    CMKExecResult result;
    exec_result_init(&result);
    if (!rt_eval_block(runtime, body, function_env, CCFALSE, &result))
    {
        exec_result_clear(&result);
        env_destroy(function_env);
        return CCFALSE;
    }

    CCBOOL ok = CCFALSE;
    if (result.kind == CMK_EXEC_RETURN)
    {
        if (!rt_expect_value(
                runtime, function->decl, &result.value,
                &function->return_type
            ))
        {
            exec_result_clear(&result);
            env_destroy(function_env);
            return CCFALSE;
        }
        ok = cmk_value_copy(out, &result.value);
        if (!ok)
            ok = rt_error(runtime, function->decl, "返回值复制失败");
    }
    else if (result.kind == CMK_EXEC_NORMAL)
    {
        if (function->return_type.kind == CMK_VALUE_VOID)
            ok = cmk_value_set_void(out);
        else
            ok = rt_error(
                runtime, function->decl,
                "非 void 函数 %s 没有返回值", function->name.data
            );
    }
    else
    {
        ok = rt_error(
            runtime, function->decl, "函数 %s 控制流异常",
            function->name.data
        );
    }

    exec_result_clear(&result);
    env_destroy(function_env);
    return ok;
}

/* ============================================================================
 * 模块配置
 * ========================================================================== */

static void target_draft_clear(CMKTargetDraft *draft)
{
    if (!draft)
        return;
    cmk_value_clear(&draft->depends_on);
    cmk_value_clear(&draft->inputs);
    cmk_value_clear(&draft->outputs);
    cmk_value_clear(&draft->actions);
    memset(draft, 0, sizeof(*draft));
}

static CCBOOL target_draft_init(
    CMKRuntime *runtime, CMKTargetDraft *draft, CMKASTNodeId decl
)
{
    memset(draft, 0, sizeof(*draft));
    draft->info = cmk_semantic_find_target_by_decl(
        runtime->semantic, decl
    );
    if (!draft->info)
        return rt_error(runtime, decl, "target 语义记录缺失");
    cmk_value_init(&draft->depends_on);
    cmk_value_init(&draft->inputs);
    cmk_value_init(&draft->outputs);
    cmk_value_init(&draft->actions);

    if (!cmk_value_set_list(&draft->depends_on, CMK_VALUE_TARGET_REF) ||
        !cmk_value_set_list(&draft->inputs, CMK_VALUE_STRING) ||
        !cmk_value_set_list(&draft->outputs, CMK_VALUE_STRING) ||
        !cmk_value_set_list(&draft->actions, CMK_VALUE_ACTION))
    {
        target_draft_clear(draft);
        return rt_error(runtime, decl, "target 默认字段分配失败");
    }
    return CCTRUE;
}

static CCBOOL rt_eval_target(
    CMKRuntime *runtime, CMKASTNodeId decl, CMKEnv *env
)
{
    CMKTargetDraft draft;
    if (!target_draft_init(runtime, &draft, decl))
        return CCFALSE;

    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, decl);
    if (!node)
    {
        target_draft_clear(&draft);
        return rt_error(runtime, decl, "target 声明缺失");
    }

    for (CMKASTNodeId field = node->first_child;
         field != CMK_AST_NODE_NIL;)
    {
        const CMKASTNode *field_node =
            cmk_ast_node(runtime->semantic->ast, field);
        if (!field_node)
        {
            target_draft_clear(&draft);
            return rt_error(runtime, decl, "target 字段缺失");
        }
        CMKASTNodeId next = field_node->next_sibling;

        CMKTargetFieldKind kind = field_node->as.target_field.field;
        CMKType expected;
        switch (kind)
        {
        case CMK_TARGET_FIELD_DEPENDS_ON:
            expected = cmk_type_list(CMK_VALUE_TARGET_REF);
            break;

        case CMK_TARGET_FIELD_INPUTS:
        case CMK_TARGET_FIELD_OUTPUTS:
            expected = cmk_type_list(CMK_VALUE_STRING);
            break;

        case CMK_TARGET_FIELD_ACTIONS:
            expected = cmk_type_list(CMK_VALUE_ACTION);
            break;

        default:
            target_draft_clear(&draft);
            return rt_error(runtime, field, "未知 target 字段");
        }

        CMKValue value;
        cmk_value_init(&value);
        CMKASTNodeId value_id = rt_child(runtime, field, 1);
        if (!rt_eval_expr(runtime, value_id, env, &value) ||
            !rt_expect_value(runtime, value_id, &value, &expected))
        {
            cmk_value_clear(&value);
            target_draft_clear(&draft);
            return CCFALSE;
        }

        CMKValue *slot = NULL;
        switch (kind)
        {
        case CMK_TARGET_FIELD_DEPENDS_ON:
            slot = &draft.depends_on;
            break;
        case CMK_TARGET_FIELD_INPUTS:
            slot = &draft.inputs;
            break;
        case CMK_TARGET_FIELD_OUTPUTS:
            slot = &draft.outputs;
            break;
        default:
            slot = &draft.actions;
            break;
        }
        cmk_value_clear(slot);
        *slot = value;
        cmk_value_init(&value);
        cmk_value_clear(&value);
        field = next;
    }

    CMKTargetDraft *drafts = (CMKTargetDraft *)runtime->targets.array;
    CCUINT32 count = runtime->targets.size /
        (CCUINT32)sizeof(CMKTargetDraft);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (drafts[i].info->name.len == draft.info->name.len &&
            memcmp(
                drafts[i].info->name.data, draft.info->name.data,
                draft.info->name.len
            ) == 0)
        {
            const CMKTargetInfo *duplicate = draft.info;
            target_draft_clear(&draft);
            return rt_error(
                runtime, decl, "同一配置中重复选择 target：%s",
                duplicate->name.data
            );
        }
    }

    if (cc_dyn_core_push(
            &runtime->targets, (CCUINT32)sizeof(CMKTargetDraft),
            (CCINT32)sizeof(CMKTargetDraft), &draft
        ) != 0)
    {
        target_draft_clear(&draft);
        return rt_error(runtime, decl, "内存分配失败");
    }
    return CCTRUE;
}

static CCBOOL rt_eval_top_targets(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "顶层 target 节点缺失");

    switch (node->kind)
    {
    case CMK_AST_TARGET_DECL:
        return rt_eval_target(runtime, id, env);

    case CMK_AST_IF:
    {
        CMKASTNodeId condition_id = node->first_child;
        CMKValue condition;
        cmk_value_init(&condition);
        if (!rt_eval_expr(runtime, condition_id, env, &condition))
        {
            cmk_value_clear(&condition);
            return CCFALSE;
        }
        if (condition.type != CMK_VALUE_BOOL)
        {
            cmk_value_clear(&condition);
            return rt_error(runtime, id, "顶层 if 条件不是 bool");
        }
        CCBOOL choose_then = condition.as.boolean;
        cmk_value_clear(&condition);

        const CMKASTNode *condition_node =
            cmk_ast_node(runtime->semantic->ast, condition_id);
        CMKASTNodeId then_id =
            condition_node ? condition_node->next_sibling : CMK_AST_NODE_NIL;
        const CMKASTNode *then_node =
            cmk_ast_node(runtime->semantic->ast, then_id);
        CMKASTNodeId else_id =
            then_node ? then_node->next_sibling : CMK_AST_NODE_NIL;

        if (choose_then)
            return rt_eval_top_targets(runtime, then_id, env);
        if (else_id != CMK_AST_NODE_NIL)
            return rt_eval_top_targets(runtime, else_id, env);
        return CCTRUE;
    }

    case CMK_AST_BLOCK:
    {
        for (CMKASTNodeId child = node->first_child;
             child != CMK_AST_NODE_NIL;)
        {
            if (!rt_eval_top_targets(runtime, child, env))
                return CCFALSE;
            const CMKASTNode *child_node =
                cmk_ast_node(runtime->semantic->ast, child);
            child = child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;
        }
        return CCTRUE;
    }

    default:
        return rt_error(runtime, id, "顶层 if 分支只能包含 target");
    }
}

CCBOOL cmk_runtime_init_globals(CMKRuntime *runtime)
{
    if (!runtime || !runtime->semantic)
        return CCFALSE;
    if (runtime->globals_initialized)
        return runtime->failed ? CCFALSE : CCTRUE;
    if (!runtime->semantic->analyzed || !runtime->semantic->ok)
        return rt_error(runtime, cmk_ast_root(runtime->semantic->ast),
                        "语义分析未通过");

    runtime->module_env = env_create(NULL);
    if (!runtime->module_env)
        return rt_error(runtime, cmk_ast_root(runtime->semantic->ast),
                        "内存分配失败");

    CMKASTNodeId root = cmk_ast_root(runtime->semantic->ast);
    const CMKASTNode *root_node =
        cmk_ast_node(runtime->semantic->ast, root);
    if (!root_node)
        return rt_error(runtime, root, "模块根节点缺失");

    for (CMKASTNodeId child = root_node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        if (!child_node)
            return rt_error(runtime, child, "模块节点缺失");
        CMKASTNodeId next = child_node->next_sibling;
        if (child_node->kind == CMK_AST_VAR_DECL)
        {
            if (!rt_eval_var_decl(runtime, child, runtime->module_env))
                return CCFALSE;
        }
        child = next;
    }

    runtime->globals_initialized = CCTRUE;
    return CCTRUE;
}

CCBOOL cmk_runtime_configure(CMKRuntime *runtime)
{
    if (!runtime || !runtime->semantic)
        return CCFALSE;
    if (runtime->configured)
        return runtime->failed ? CCFALSE : CCTRUE;

    if (!cmk_runtime_init_globals(runtime))
    {
        runtime->configured = CCTRUE;
        return CCFALSE;
    }
    runtime->configured = CCTRUE;

    CMKASTNodeId root = cmk_ast_root(runtime->semantic->ast);
    const CMKASTNode *root_node =
        cmk_ast_node(runtime->semantic->ast, root);
    if (!root_node)
        return rt_error(runtime, root, "模块根节点缺失");

    for (CMKASTNodeId child = root_node->first_child;
         child != CMK_AST_NODE_NIL;)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        if (!child_node)
            return rt_error(runtime, child, "模块节点缺失");
        CMKASTNodeId next = child_node->next_sibling;
        if (child_node->kind == CMK_AST_TARGET_DECL ||
            child_node->kind == CMK_AST_IF)
        {
            if (!rt_eval_top_targets(runtime, child, runtime->module_env))
                return CCFALSE;
        }
        child = next;
    }

    return CCTRUE;
}

void cmk_runtime_set_module_resolver(
    CMKRuntime *runtime,
    CMKRuntimeModuleResolver resolver, void *user
)
{
    if (!runtime)
        return;
    runtime->module_resolver = resolver;
    runtime->resolver_user = user;
}

void *cmk_runtime_get_resolver_user(const CMKRuntime *runtime)
{
    return runtime ? runtime->resolver_user : NULL;
}

void cmk_runtime_set_work_dir(CMKRuntime *runtime, const char *work_dir)
{
    if (runtime)
        runtime->work_dir = work_dir;
}

const char *cmk_runtime_get_work_dir(const CMKRuntime *runtime)
{
    return runtime ? runtime->work_dir : NULL;
}

CCBOOL cmk_runtime_is_configured(const CMKRuntime *runtime)
{
    return (runtime && runtime->configured) ? CCTRUE : CCFALSE;
}

CCUINT32 cmk_runtime_target_count(const CMKRuntime *runtime)
{
    if (!runtime)
        return 0;
    return runtime->targets.size / (CCUINT32)sizeof(CMKTargetDraft);
}

const CMKTargetDraft *cmk_runtime_target_at(
    const CMKRuntime *runtime, CCUINT32 index
)
{
    if (!runtime || index >= cmk_runtime_target_count(runtime))
        return NULL;
    return &((const CMKTargetDraft *)runtime->targets.array)[index];
}

CCBOOL cmk_runtime_get_global(
    const CMKRuntime *runtime,
    const char *name, CCUINT32 name_len,
    CMKValue *out
)
{
    if (!runtime || !runtime->module_env || !name || !out)
        return CCFALSE;
    CMKEnvVar *slot = env_slot(runtime->module_env, name, name_len);
    if (!slot)
        return CCFALSE;
    return cmk_value_copy(out, &slot->value);
}

/* ============================================================================
 * 生命周期
 * ========================================================================== */

CMKRuntime *cmk_runtime_create(
    CMKSemanticModule *semantic,
    CMKConfig *config,
    CMKBuiltinRegistry *builtins,
    CMKDiagnostic *diagnostic
)
{
    if (!semantic || !config || !builtins || !diagnostic)
        return NULL;

    CMKRuntime *runtime =
        ccalloc(NULL, sizeof(CMKRuntime), CMK_RT_COMMENT);
    if (!runtime)
        return NULL;
    memset(runtime, 0, sizeof(*runtime));
    runtime->semantic = semantic;
    runtime->config = config;
    runtime->builtins = builtins;
    runtime->diagnostic = diagnostic;
    return runtime;
}

void cmk_runtime_destroy(CMKRuntime *runtime)
{
    if (!runtime)
        return;
    CMKTargetDraft *drafts = (CMKTargetDraft *)runtime->targets.array;
    CCUINT32 count = runtime->targets.size /
        (CCUINT32)sizeof(CMKTargetDraft);
    for (CCUINT32 i = 0; i < count; i++)
        target_draft_clear(&drafts[i]);
    if (runtime->targets.array)
        ccalloc(runtime->targets.array, 0, NULL);
    if (runtime->module_env)
        env_destroy(runtime->module_env);
    ccalloc(runtime, 0, NULL);
}
