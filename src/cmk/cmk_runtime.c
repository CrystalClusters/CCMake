/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK Configure 求值运行时
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_runtime.h"
#include "cmk_semantic_internal.h"
#include "cmk_safe.h"
#include "cmk_exec_stack.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
    CMK_EXEC_ERROR,
    CMK_EXEC_TAILCALL,
    CMK_EXEC_DEFER,
    CMK_EXEC_EXPR_DEFER
} CMKExecKind;

typedef struct cmk_expr_program CMKExprProgram;

typedef enum cmk_continuation_kind {
    CMK_CONT_BINARY = 0,
    CMK_CONT_UNARY
} CMKContinuationKind;

typedef struct cmk_continuation {
    CMKContinuationKind kind;
    CMKBinaryOp binary;
    CMKUnaryOp unary;
    CMKValue operand;
} CMKContinuation;

typedef struct cmk_exec_result {
    CMKExecKind kind;
    CMKValue value;
    const CMKFunctionInfo *tail_function;
    CMKValue *tail_args;
    CCUINT32 tail_argc;
    const CMKFunctionInfo *defer_function;
    CMKValue *defer_args;
    CCUINT32 defer_argc;
    CCDyn continuations;
    CMKExprProgram *expr_program;
    const CMKFunctionInfo *expr_function;
} CMKExecResult;

typedef struct cmk_cont_frame {
    CCDyn cont;
} CMKContFrame;

struct cmk_runtime {
    CMKSemanticModule *semantic;
    CMKConfig *config;
    CMKBuiltinRegistry *builtins;
    CMKDiagnostic *diagnostic;
    CMKEnv *module_env;
    CCDyn globals;
    CMKRuntimeModuleResolver module_resolver;
    void *resolver_user;
    const char *work_dir;
    CMKExecutionPolicy policy;
    CCUINT64 steps;
    CCUINT32 call_depth;
    clock_t configure_start;
    CCBOOL timer_started;
    CCBOOL globals_initialized;
    CCBOOL configured;
    CCBOOL failed;
};

/* ============================================================================
 * 诊断与执行结果
 * ========================================================================== */

static CCBOOL rt_step(CMKRuntime *runtime, CMKASTNodeId node);

static CCBOOL rt_error(
    CMKRuntime *runtime, CMKASTNodeId node, const char *fmt, ...
)
{    if (!runtime || !runtime->diagnostic)
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

static void rt_expr_program_clear(CMKExprProgram *program);

static void exec_result_clear(CMKExecResult *result)
{
    if (!result)
        return;
    cmk_value_clear(&result->value);
    if (result->tail_args)
    {
        for (CCUINT32 i = 0; i < result->tail_argc; i++)
            cmk_value_clear(&result->tail_args[i]);
        ccalloc(result->tail_args, 0, NULL);
    }
    if (result->defer_args)
    {
        for (CCUINT32 i = 0; i < result->defer_argc; i++)
            cmk_value_clear(&result->defer_args[i]);
        ccalloc(result->defer_args, 0, NULL);
    }
    CMKContinuation *conts = (CMKContinuation *)result->continuations.array;
    CCUINT32 cont_count = result->continuations.size /
        (CCUINT32)sizeof(CMKContinuation);
    for (CCUINT32 i = 0; i < cont_count; i++)
        cmk_value_clear(&conts[i].operand);
    if (result->continuations.array)
        ccalloc(result->continuations.array, 0, NULL);
    if (result->expr_program)
    {
        rt_expr_program_clear(result->expr_program);
        ccalloc(result->expr_program, 0, NULL);
    }
    result->expr_program = NULL;
    result->expr_function = NULL;
    result->tail_args = NULL;
    result->tail_argc = 0;
    result->tail_function = NULL;
    result->defer_args = NULL;
    result->defer_argc = 0;
    result->defer_function = NULL;
    memset(&result->continuations, 0, sizeof(result->continuations));
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
    if (!cmk_dyn_push(
            &env->vars, (CCUINT32)sizeof(CMKEnvVar),
            (CCINT32)sizeof(CMKEnvVar), &var
        ))
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


static CCBOOL rt_eval_ident(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
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




static CCBOOL rt_apply_unary(
    CMKRuntime *runtime, CMKASTNodeId id, CMKUnaryOp op,
    const CMKValue *value, CMKValue *out
)
{
    switch (op)
    {
    case CMK_UNARY_NOT:
        if (value->type != CMK_VALUE_BOOL)
            return rt_error(runtime, id, "! 需要 bool");
        return cmk_value_set_bool(
            out, value->as.boolean ? CCFALSE : CCTRUE
        );

    case CMK_UNARY_NEG:
        if (value->type == CMK_VALUE_INT)
        {
            if (value->as.integer == CMK_INT64_MIN_)
                return rt_error(runtime, id, "整数取负溢出");
            return cmk_value_set_int(out, -value->as.integer);
        }
        if (value->type == CMK_VALUE_FLOAT)
            return cmk_value_set_float(out, -value->as.floating);
        return rt_error(runtime, id, "- 需要数值");

    case CMK_UNARY_POS:
        if (value->type == CMK_VALUE_INT ||
            value->type == CMK_VALUE_FLOAT)
            return cmk_value_copy(out, value);
        return rt_error(runtime, id, "+ 需要数值");

    default:
        return rt_error(runtime, id, "未知一元运算符");
    }
}

static CCBOOL rt_apply_binary(
    CMKRuntime *runtime, CMKASTNodeId id, CMKBinaryOp op,
    const CMKValue *left, const CMKValue *right, CMKValue *out
)
{
    switch (op)
    {
    case CMK_BINARY_EQ:
    case CMK_BINARY_NE:
        if (left->type != right->type)
            return rt_error(runtime, id, "比较两侧类型不一致");
        {
            CCBOOL equal = cmk_value_equal(left, right);
            if (op == CMK_BINARY_NE)
                equal = equal ? CCFALSE : CCTRUE;
            return cmk_value_set_bool(out, equal);
        }

    case CMK_BINARY_LT:
    case CMK_BINARY_LE:
    case CMK_BINARY_GT:
    case CMK_BINARY_GE:
    {
        int cmp = 0;
        if (left->type == CMK_VALUE_INT && right->type == CMK_VALUE_INT)
            cmp = left->as.integer < right->as.integer ? -1
                : (left->as.integer > right->as.integer ? 1 : 0);
        else if (left->type == CMK_VALUE_FLOAT &&
                 right->type == CMK_VALUE_FLOAT)
            cmp = left->as.floating < right->as.floating ? -1
                : (left->as.floating > right->as.floating ? 1 : 0);
        else if (left->type == CMK_VALUE_STRING &&
                 right->type == CMK_VALUE_STRING)
            cmp = string_compare(&left->as.string, &right->as.string);
        else
            return rt_error(runtime, id, "比较类型不受支持");
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
        return cmk_value_set_bool(out, result);
    }

    case CMK_BINARY_ADD:
        if (left->type == CMK_VALUE_INT && right->type == CMK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_add(left->as.integer, right->as.integer, &value))
                return rt_error(runtime, id, "整数加法溢出");
            return cmk_value_set_int(out, value);
        }
        if (left->type == CMK_VALUE_FLOAT &&
            right->type == CMK_VALUE_FLOAT)
            return cmk_value_set_float(
                out, left->as.floating + right->as.floating
            );
        if (left->type == CMK_VALUE_STRING &&
            right->type == CMK_VALUE_STRING)
        {
            CMKString text;
            cmk_string_init(&text);
            if (!cmk_string_append(
                    &text, left->as.string.data, left->as.string.len
                ) ||
                !cmk_string_append(
                    &text, right->as.string.data, right->as.string.len
                ))
            {
                cmk_string_clear(&text);
                return rt_error(
                    runtime, id, "字符串拼接内存分配失败"
                );
            }
            CCBOOL ok = cmk_value_set_string(out, text.data, text.len);
            cmk_string_clear(&text);
            return ok;
        }
        if (left->type == CMK_VALUE_LIST && right->type == CMK_VALUE_LIST)
        {
            CMKValueType element = left->as.list->element_type;
            if (element == CMK_VALUE_VOID)
                element = right->as.list->element_type;
            if (element == CMK_VALUE_VOID)
                return rt_error(runtime, id, "空列表类型未解析");
            if (!cmk_value_set_list(out, element))
                return rt_error(runtime, id, "列表分配失败");
            CCUINT32 left_count = cmk_list_count(left->as.list);
            for (CCUINT32 i = 0; i < left_count; i++)
            {
                if (!cmk_list_append(
                        out->as.list, cmk_list_at(left->as.list, i)
                    ))
                {
                    cmk_value_clear(out);
                    return rt_error(runtime, id, "列表拼接失败");
                }
            }
            CCUINT32 right_count = cmk_list_count(right->as.list);
            for (CCUINT32 i = 0; i < right_count; i++)
            {
                if (!cmk_list_append(
                        out->as.list, cmk_list_at(right->as.list, i)
                    ))
                {
                    cmk_value_clear(out);
                    return rt_error(runtime, id, "列表拼接失败");
                }
            }
            return CCTRUE;
        }
        return rt_error(runtime, id, "加法类型不受支持");

    case CMK_BINARY_SUB:
        if (left->type == CMK_VALUE_INT && right->type == CMK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_sub(left->as.integer, right->as.integer, &value))
                return rt_error(runtime, id, "整数减法溢出");
            return cmk_value_set_int(out, value);
        }
        if (left->type == CMK_VALUE_FLOAT &&
            right->type == CMK_VALUE_FLOAT)
            return cmk_value_set_float(
                out, left->as.floating - right->as.floating
            );
        return rt_error(runtime, id, "减法类型不受支持");

    case CMK_BINARY_MUL:
        if (left->type == CMK_VALUE_INT && right->type == CMK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_mul(left->as.integer, right->as.integer, &value))
                return rt_error(runtime, id, "整数乘法溢出");
            return cmk_value_set_int(out, value);
        }
        if (left->type == CMK_VALUE_FLOAT &&
            right->type == CMK_VALUE_FLOAT)
            return cmk_value_set_float(
                out, left->as.floating * right->as.floating
            );
        return rt_error(runtime, id, "乘法类型不受支持");

    case CMK_BINARY_DIV:
        if (left->type == CMK_VALUE_INT && right->type == CMK_VALUE_INT)
        {
            if (right->as.integer == 0)
                return rt_error(runtime, id, "整数除数为零");
            if (left->as.integer == CMK_INT64_MIN_ &&
                right->as.integer == -1)
                return rt_error(runtime, id, "整数除法溢出");
            return cmk_value_set_int(
                out, left->as.integer / right->as.integer
            );
        }
        if (left->type == CMK_VALUE_FLOAT &&
            right->type == CMK_VALUE_FLOAT)
        {
            if (right->as.floating == 0.0)
                return rt_error(runtime, id, "浮点除数为零");
            return cmk_value_set_float(
                out, left->as.floating / right->as.floating
            );
        }
        return rt_error(runtime, id, "除法类型不受支持");

    case CMK_BINARY_MOD:
        if (left->type != CMK_VALUE_INT || right->type != CMK_VALUE_INT)
            return rt_error(runtime, id, "取模需要 int");
        if (right->as.integer == 0)
            return rt_error(runtime, id, "取模除数为零");
        if (left->as.integer == CMK_INT64_MIN_ &&
            right->as.integer == -1)
            return rt_error(runtime, id, "取模溢出");
        return cmk_value_set_int(
            out, left->as.integer % right->as.integer
        );

    default:
        return rt_error(runtime, id, "未知二元运算符");
    }
}

typedef enum cmk_rt_expr_kind {
    CMK_RT_EXPR_LITERAL = 0,
    CMK_RT_EXPR_IDENT,
    CMK_RT_EXPR_CONFIG,
    CMK_RT_EXPR_MEMBER,
    CMK_RT_EXPR_LIST,
    CMK_RT_EXPR_UNARY,
    CMK_RT_EXPR_BINARY,
    CMK_RT_EXPR_INDEX,
    CMK_RT_EXPR_CALL
} CMKRtExprKind;

typedef enum cmk_rt_expr_stage {
    CMK_RT_EXPR_START = 0,
    CMK_RT_EXPR_LIST_ITEM,
    CMK_RT_EXPR_UNARY_OPERAND,
    CMK_RT_EXPR_BINARY_LEFT,
    CMK_RT_EXPR_BINARY_RIGHT,
    CMK_RT_EXPR_INDEX_BASE,
    CMK_RT_EXPR_INDEX_INDEX
} CMKRtExprStage;

typedef struct cmk_rt_expr_frame {
    CMKRtExprKind kind;
    CMKRtExprStage stage;
    CMKASTNodeId id;
    CMKEnv *env;
    CMKValue value;
    CMKBinaryOp op;
    CMKUnaryOp unary_op;
    CCUINT32 index;
    CCUINT32 count;
    CMKValue *args;
    CCUINT32 argc;
    const CMKFunctionInfo *function;
    const CMKBuiltinSpec *builtin;
} CMKRtExprFrame;

static CMKRtExprFrame *rt_expr_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(CMKRtExprFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKRtExprFrame);
    return &((CMKRtExprFrame *)frames->array)[count - 1];
}

static CCBOOL rt_expr_push(
    CMKRuntime *runtime, CCDyn *frames,
    CMKASTNodeId id, CMKEnv *env
)
{
    CMKRtExprFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = CMK_RT_EXPR_LITERAL;
    frame.stage = CMK_RT_EXPR_START;
    frame.id = id;
    frame.env = env;
    cmk_value_init(&frame.value);
    if (!cmk_dyn_push(
            frames, (CCUINT32)sizeof(CMKRtExprFrame),
            (CCINT32)sizeof(CMKRtExprFrame), &frame
        ))
        return rt_error(runtime, id, "内存分配失败");
    return CCTRUE;
}

static CCBOOL rt_expr_pop(CCDyn *frames, CMKRtExprFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(CMKRtExprFrame),
        (CCINT32)sizeof(CMKRtExprFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void rt_expr_frame_cleanup(CMKRtExprFrame *frame)
{
    cmk_value_clear(&frame->value);
    if (frame->args)
    {
        for (CCUINT32 i = 0; i < frame->argc; i++)
            cmk_value_clear(&frame->args[i]);
        ccalloc(frame->args, 0, NULL);
        frame->args = NULL;
        frame->argc = 0;
    }
}

static void rt_expr_frames_abort(CCDyn *frames)
{
    CCUINT32 count = frames->size / (CCUINT32)sizeof(CMKRtExprFrame);
    for (CCUINT32 i = 0; i < count; i++)
        rt_expr_frame_cleanup(&((CMKRtExprFrame *)frames->array)[i]);
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL rt_expr_list_next(
    CMKRuntime *runtime, CMKRtExprFrame *frame, CCDyn *frames
)
{
    if (frame->index >= frame->count)
        return CCTRUE;
    CMKASTNodeId item = rt_child(runtime, frame->id, frame->index);
    if (item == CMK_AST_NODE_NIL)
        return rt_error(runtime, frame->id, "列表元素缺失");
    return rt_expr_push(runtime, frames, item, frame->env);
}

static CCBOOL rt_invoke_call(
    CMKRuntime *runtime, CMKASTNodeId id,
    const CMKFunctionInfo *function, const CMKBuiltinSpec *spec,
    CMKValue *args, CCUINT32 argc, CMKValue *out
)
{
    if (function)
        return cmk_runtime_call_function(
            runtime, function, args, argc, out
        );
    if (!spec)
        return rt_error(runtime, id, "调用未完成语义绑定");

    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "调用节点缺失");

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
        if (runtime->failed)
            return CCFALSE;
        return rt_error(runtime, id, "内建调用失败：%s", spec->name);
    }

    CMKType actual = value_type(&result);
    if (!cmk_type_equal(&actual, &spec->return_type))
    {
        cmk_value_clear(&result);
        return rt_error(runtime, id, "内建返回值类型错误：%s", spec->name);
    }

    ok = cmk_value_copy(out, &result);
    cmk_value_clear(&result);
    return ok ? CCTRUE : rt_error(runtime, id, "返回值复制失败");
}

static CCBOOL rt_expr_call_next(
    CMKRuntime *runtime, CMKRtExprFrame *frame, CCDyn *frames
)
{
    if (frame->index >= frame->argc)
        return CCTRUE;
    CMKASTNodeId arg = rt_child(runtime, frame->id, frame->index);
    if (arg == CMK_AST_NODE_NIL)
        return rt_error(runtime, frame->id, "调用实参缺失");
    return rt_expr_push(runtime, frames, arg, frame->env);
}

static CCBOOL rt_expr_literal(
    CMKRuntime *runtime, CMKASTNodeId id,
    const CMKASTNode *node, CMKValue *out, CCBOOL *done
)
{
    switch (node->as.literal.kind)
    {
    case CMK_LITERAL_INT:
        *done = CCTRUE;
        return cmk_value_set_int(out, node->as.literal.value.integer);
    case CMK_LITERAL_FLOAT:
        *done = CCTRUE;
        return cmk_value_set_float(out, node->as.literal.value.floating);
    case CMK_LITERAL_STRING:
    {
        CCUINT32 len = 0;
        const char *text = cmk_ast_string(
            runtime->semantic->ast,
            node->as.literal.value.string, &len
        );
        *done = CCTRUE;
        return text
            ? cmk_value_set_string(out, text, len)
            : rt_error(runtime, id, "字符串值无效");
    }
    case CMK_LITERAL_BOOL:
        *done = CCTRUE;
        return cmk_value_set_bool(out, node->as.literal.value.boolean);
    default:
        return rt_error(runtime, id, "未知字面量");
    }
}

static CCBOOL rt_expr_step_start(
    CMKRuntime *runtime, CMKRtExprFrame *frame, CCDyn *frames,
    CMKValue *input, CMKValue *out, CCBOOL *done
)
{
    (void)frames;
    (void)input;
    if (!rt_step(runtime, frame->id))
        return CCFALSE;
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, frame->id);
    if (!node)
        return rt_error(runtime, frame->id, "表达式节点缺失");

    switch (node->kind)
    {
    case CMK_AST_LITERAL:
        return rt_expr_literal(runtime, frame->id, node, out, done);

    case CMK_AST_IDENT:
        frame->kind = CMK_RT_EXPR_IDENT;
        *done = CCTRUE;
        return rt_eval_ident(runtime, frame->id, frame->env, out);

    case CMK_AST_CONFIG_REF:
        frame->kind = CMK_RT_EXPR_CONFIG;
        *done = CCTRUE;
        return rt_eval_config(runtime, frame->id, out);

    case CMK_AST_LIST:
    {
        const CMKType *type = cmk_semantic_node_type(
            runtime->semantic, frame->id
        );
        if (!type || type->kind != CMK_VALUE_LIST ||
            type->element == CMK_VALUE_VOID)
            return rt_error(
                runtime, frame->id, "空列表或列表元素类型未解析"
            );
        frame->kind = CMK_RT_EXPR_LIST;
        frame->index = 0;
        frame->count = 0;
        for (CMKASTNodeId child = node->first_child;
             child != CMK_AST_NODE_NIL;)
        {
            frame->count++;
            const CMKASTNode *child_node =
                cmk_ast_node(runtime->semantic->ast, child);
            child = child_node
                ? child_node->next_sibling : CMK_AST_NODE_NIL;
        }
        if (!cmk_value_set_list(&frame->value, type->element))
            return rt_error(runtime, frame->id, "列表分配失败");
        if (frame->count > 0)
        {
            if (!rt_expr_list_next(runtime, frame, frames))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }
        *out = frame->value;
        cmk_value_init(&frame->value);
        *done = CCTRUE;
        return CCTRUE;
    }

    case CMK_AST_UNARY:
        frame->kind = CMK_RT_EXPR_UNARY;
        frame->unary_op = node->as.unary.op;
        frame->stage = CMK_RT_EXPR_UNARY_OPERAND;
        if (!rt_expr_push(
                runtime, frames, node->first_child, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;

    case CMK_AST_BINARY:
        frame->kind = CMK_RT_EXPR_BINARY;
        frame->op = node->as.binary.op;
        frame->stage = CMK_RT_EXPR_BINARY_LEFT;
        if (!rt_expr_push(
                runtime, frames, node->first_child, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;

    case CMK_AST_INDEX:
        frame->kind = CMK_RT_EXPR_INDEX;
        frame->stage = CMK_RT_EXPR_INDEX_BASE;
        if (!rt_expr_push(
                runtime, frames, node->first_child, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;

    case CMK_AST_CALL:
    {
        frame->kind = CMK_RT_EXPR_CALL;
        frame->index = 0;
        frame->argc = 0;
        for (CMKASTNodeId child = node->first_child;
             child != CMK_AST_NODE_NIL;)
        {
            frame->argc++;
            const CMKASTNode *child_node =
                cmk_ast_node(runtime->semantic->ast, child);
            child = child_node
                ? child_node->next_sibling : CMK_AST_NODE_NIL;
        }
        frame->function = cmk_semantic_node_function(
            runtime->semantic, frame->id
        );
        frame->builtin = cmk_semantic_node_builtin(
            runtime->semantic, frame->id
        );
        if (!frame->function && !frame->builtin)
            return rt_error(
                runtime, frame->id, "调用未完成语义绑定"
            );
        if (frame->argc > 0)
        {
            frame->args = ccalloc(
                NULL,
                (CCUINT64)frame->argc * sizeof(CMKValue),
                CMK_RT_COMMENT
            );
            if (!frame->args)
                return rt_error(runtime, frame->id, "内存分配失败");
            for (CCUINT32 i = 0; i < frame->argc; i++)
                cmk_value_init(&frame->args[i]);
            if (!rt_expr_call_next(runtime, frame, frames))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }
        if (!rt_invoke_call(
                runtime, frame->id, frame->function,
                frame->builtin, NULL, 0, out
            ))
            return CCFALSE;
        *done = CCTRUE;
        return CCTRUE;
    }

    default:
        return rt_error(
            runtime, frame->id, "不支持的表达式节点：%s",
            cmk_ast_kind_name(node->kind)
        );
    }
}

static CCBOOL rt_expr_step_resume(
    CMKRuntime *runtime, CMKRtExprFrame *frame, CCDyn *frames,
    CMKValue *input, CMKValue *out, CCBOOL *done
)
{
    switch (frame->kind)
    {
case CMK_RT_EXPR_LIST:
    if (!cmk_list_append(frame->value.as.list, input))
        return rt_error(runtime, frame->id, "列表追加失败");
    cmk_value_clear(input);
    frame->index++;
    if (frame->index >= frame->count)
    {
        *out = frame->value;
        cmk_value_init(&frame->value);
        *done = CCTRUE;
        return CCTRUE;
    }
    if (!rt_expr_list_next(runtime, frame, frames))
        return CCFALSE;
    *done = CCFALSE;
    return CCTRUE;

case CMK_RT_EXPR_UNARY:
    if (!rt_apply_unary(
            runtime, frame->id, frame->unary_op, input, out
        ))
        return CCFALSE;
    cmk_value_clear(input);
    *done = CCTRUE;
    return CCTRUE;

case CMK_RT_EXPR_BINARY:
    if (frame->stage == CMK_RT_EXPR_BINARY_LEFT)
    {
        if (frame->op == CMK_BINARY_AND ||
            frame->op == CMK_BINARY_OR)
        {
            if (input->type != CMK_VALUE_BOOL)
            {
                cmk_value_clear(input);
                return rt_error(
                    runtime, frame->id, "逻辑运算需要 bool"
                );
            }
            if (frame->op == CMK_BINARY_AND && !input->as.boolean)
            {
                cmk_value_clear(input);
                *done = CCTRUE;
                return cmk_value_set_bool(out, CCFALSE);
            }
            if (frame->op == CMK_BINARY_OR && input->as.boolean)
            {
                cmk_value_clear(input);
                *done = CCTRUE;
                return cmk_value_set_bool(out, CCTRUE);
            }
        }
        frame->value = *input;
        cmk_value_init(input);
        frame->stage = CMK_RT_EXPR_BINARY_RIGHT;
        const CMKASTNode *node =
            cmk_ast_node(runtime->semantic->ast, frame->id);
        CMKASTNodeId right_id = node
            ? node->first_child : CMK_AST_NODE_NIL;
        const CMKASTNode *right_node = cmk_ast_node(
            runtime->semantic->ast, right_id
        );
        right_id = right_node
            ? right_node->next_sibling : CMK_AST_NODE_NIL;
        if (right_id == CMK_AST_NODE_NIL)
            return rt_error(
                runtime, frame->id, "二元表达式右操作数缺失"
            );
        if (!rt_expr_push(
                runtime, frames, right_id, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    if (frame->op == CMK_BINARY_AND ||
        frame->op == CMK_BINARY_OR)
    {
        if (input->type != CMK_VALUE_BOOL)
        {
            cmk_value_clear(input);
            cmk_value_clear(&frame->value);
            return rt_error(
                runtime, frame->id, "逻辑运算需要 bool"
            );
        }
        CCBOOL result = input->as.boolean;
        cmk_value_clear(input);
        cmk_value_clear(&frame->value);
        *done = CCTRUE;
        return cmk_value_set_bool(out, result);
    }

    if (!rt_apply_binary(
            runtime, frame->id, frame->op,
            &frame->value, input, out
        ))
    {
        cmk_value_clear(&frame->value);
        cmk_value_clear(input);
        return CCFALSE;
    }
    cmk_value_clear(&frame->value);
    cmk_value_clear(input);
    *done = CCTRUE;
    return CCTRUE;

case CMK_RT_EXPR_INDEX:
    if (frame->stage == CMK_RT_EXPR_INDEX_BASE)
    {
        frame->value = *input;
        cmk_value_init(input);
        frame->stage = CMK_RT_EXPR_INDEX_INDEX;
        const CMKASTNode *node =
            cmk_ast_node(runtime->semantic->ast, frame->id);
        CMKASTNodeId index_id = node
            ? node->first_child : CMK_AST_NODE_NIL;
        const CMKASTNode *index_node = cmk_ast_node(
            runtime->semantic->ast, index_id
        );
        index_id = index_node
            ? index_node->next_sibling : CMK_AST_NODE_NIL;
        if (index_id == CMK_AST_NODE_NIL)
            return rt_error(
                runtime, frame->id, "索引下标缺失"
            );
        if (!rt_expr_push(
                runtime, frames, index_id, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    if (frame->value.type != CMK_VALUE_LIST ||
        input->type != CMK_VALUE_INT)
    {
        cmk_value_clear(&frame->value);
        cmk_value_clear(input);
        return rt_error(runtime, frame->id, "索引需要 list[int]");
    }
    {
        CCINT64 at = input->as.integer;
        if (at < 0 ||
            (CCUINT64)at >= cmk_list_count(frame->value.as.list))
        {
            cmk_value_clear(&frame->value);
            cmk_value_clear(input);
            return rt_error(runtime, frame->id, "列表索引越界");
        }
        CCBOOL ok = cmk_value_copy(
            out, cmk_list_at(frame->value.as.list, (CCUINT32)at)
        );
        cmk_value_clear(&frame->value);
        cmk_value_clear(input);
        *done = CCTRUE;
        return ok ? CCTRUE : rt_error(
            runtime, frame->id, "索引值复制失败"
        );
    }

case CMK_RT_EXPR_CALL:
    frame->args[frame->index] = *input;
    cmk_value_init(input);
    frame->index++;
    if (frame->index < frame->argc)
    {
        if (!rt_expr_call_next(runtime, frame, frames))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;
    }

    if (!rt_invoke_call(
            runtime, frame->id, frame->function,
            frame->builtin, frame->args, frame->argc, out
        ))
        return CCFALSE;
    for (CCUINT32 i = 0; i < frame->argc; i++)
        cmk_value_clear(&frame->args[i]);
    ccalloc(frame->args, 0, NULL);
    frame->args = NULL;
    frame->argc = 0;
    *done = CCTRUE;
    return CCTRUE;

default:
    return CCFALSE;
    }
}

static CCBOOL rt_expr_step(
    CMKRuntime *runtime, CMKRtExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, CMKValue *input, CMKValue *out, CCBOOL *done
)
{
    if (!has_input)
        return rt_expr_step_start(runtime, frame, frames, input, out, done);
    return rt_expr_step_resume(runtime, frame, frames, input, out, done);
}

static CCBOOL rt_eval_expr_run(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CCBOOL has_completed = CCFALSE;
    CMKValue completed;
    cmk_value_init(&completed);

    for (;;)
    {
        if (has_completed)
        {
            CMKRtExprFrame *frame = rt_expr_top(&frames);
            if (!frame)
            {
                *out = completed;
                cmk_value_init(&completed);
                ccalloc(frames.array, 0, NULL);
                return CCTRUE;
            }
            CMKValue result;
            cmk_value_init(&result);
            CCBOOL done = CCFALSE;
            if (!rt_expr_step(
                    runtime, frame, &frames, CCTRUE, &completed,
                    &result, &done
                ))
                goto Failed;
            if (done)
            {
                rt_expr_frame_cleanup(frame);
                CMKRtExprFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!rt_expr_pop(&frames, &popped))
                    goto Failed;
                completed = result;
                cmk_value_init(&result);
            }
            else
            {
                has_completed = CCFALSE;
            }
            continue;
        }

        if (frames.size == 0)
        {
            if (!rt_expr_push(runtime, &frames, id, env))
                goto Failed;
            continue;
        }

        CMKRtExprFrame *frame = rt_expr_top(&frames);
        if (!frame)
            goto Failed;
        CMKValue result;
        cmk_value_init(&result);
        CCBOOL done = CCFALSE;
        if (!rt_expr_step(
                runtime, frame, &frames, CCFALSE, NULL,
                &result, &done
            ))
            goto Failed;
        if (done)
        {
            rt_expr_frame_cleanup(frame);
            CMKRtExprFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!rt_expr_pop(&frames, &popped))
                goto Failed;
            completed = result;
            cmk_value_init(&result);
            has_completed = CCTRUE;
        }
    }

Failed:
    cmk_value_clear(&completed);
    rt_expr_frames_abort(&frames);
    return CCFALSE;
}


static CCBOOL rt_eval_expr(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env, CMKValue *out
)
{
    if (!runtime || !runtime->semantic || runtime->failed)
        return CCFALSE;
    return rt_eval_expr_run(runtime, id, env, out);
}

/* ============================================================================
 * 语句求值
 * ========================================================================== */


static CCBOOL rt_var_decl_apply(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CMKValue *value
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_VAR_DECL)
    {
        cmk_value_clear(value);
        return rt_error(runtime, id, "内部错误：变量声明");
    }

    CCUINT32 name_len = 0;
    const char *name = cmk_ast_string(
        runtime->semantic->ast, node->as.var_decl.name, &name_len
    );
    CCBOOL ok = name && env_define(
        runtime, env, name, name_len, value,
        node->as.var_decl.is_const, id
    );
    if (!ok && !name)
        ok = rt_error(runtime, id, "变量名无效");
    if (!ok && name && !cmk_diagnostic_has(runtime->diagnostic))
        ok = rt_error(runtime, id, "变量 %s 初始化失败", name);
    cmk_value_clear(value);
    return ok;
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

    return rt_var_decl_apply(runtime, id, env, &value);
}

static CCBOOL rt_assign_apply(
    CMKRuntime *runtime, CMKASTNodeId id, CMKEnv *env,
    CMKValue *value
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != CMK_AST_ASSIGN)
    {
        cmk_value_clear(value);
        return rt_error(runtime, id, "内部错误：赋值语句");
    }
    CMKASTNodeId target_id = node->first_child;
    const CMKASTNode *target =
        cmk_ast_node(runtime->semantic->ast, target_id);
    if (!target)
    {
        cmk_value_clear(value);
        return rt_error(runtime, id, "赋值目标缺失");
    }
    if (target->kind == CMK_AST_DISCARD)
    {
        cmk_value_clear(value);
        return CCTRUE;
    }
    if (target->kind != CMK_AST_IDENT)
    {
        cmk_value_clear(value);
        return rt_error(runtime, id, "赋值目标非法");
    }

    CCUINT32 name_len = 0;
    const char *name = cmk_ast_string(
        runtime->semantic->ast, target->as.ident.name, &name_len
    );
    if (!name)
    {
        cmk_value_clear(value);
        return rt_error(runtime, id, "变量名无效");
    }
    CMKEnvVar *slot = env_slot(env, name, name_len);
    if (!slot)
    {
        cmk_value_clear(value);
        return rt_error(runtime, id, "未定义的变量：%.*s", (int)name_len, name);
    }
    if (slot->is_const)
    {
        cmk_value_clear(value);
        return rt_error(
            runtime, id, "const 变量不能重新赋值：%.*s",
            (int)name_len, name
        );
    }

    CCBOOL ok = CCFALSE;
    if (node->as.assign.op == CMK_ASSIGN_SET)
    {
        ok = cmk_value_copy(&slot->value, value);
        if (!ok)
            ok = rt_error(runtime, id, "值复制失败");
    }
    else if (slot->value.type == CMK_VALUE_LIST)
    {
        if (value->type != slot->value.as.list->element_type)
            ok = rt_error(runtime, id, "list += 元素类型不匹配");
        else
            ok = cmk_list_append(slot->value.as.list, value)
                ? CCTRUE : rt_error(runtime, id, "list += 追加失败");
    }
    else if (slot->value.type == CMK_VALUE_INT && value->type == CMK_VALUE_INT)
    {
        CCINT64 sum = 0;
        if (!int_add(slot->value.as.integer, value->as.integer, &sum))
            ok = rt_error(runtime, id, "整数 += 溢出");
        else
            ok = cmk_value_set_int(&slot->value, sum);
    }
    else if (slot->value.type == CMK_VALUE_FLOAT &&
             value->type == CMK_VALUE_FLOAT)
        ok = cmk_value_set_float(
            &slot->value, slot->value.as.floating + value->as.floating
        );
    else
        ok = rt_error(runtime, id, "+= 类型不受支持");

    cmk_value_clear(value);
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

    return rt_assign_apply(runtime, id, env, &value);
}

typedef enum cmk_rt_frame_kind {
    CMK_RT_FRAME_BLOCK = 0,
    CMK_RT_FRAME_IF,
    CMK_RT_FRAME_WHILE,
    CMK_RT_FRAME_FOR
} CMKRtFrameKind;

typedef enum cmk_rt_frame_stage {
    CMK_RT_STAGE_ITEMS = 0,
    CMK_RT_STAGE_BODY,
    CMK_RT_STAGE_DONE
} CMKRtFrameStage;

typedef struct cmk_rt_frame {
    CMKRtFrameKind kind;
    CMKRtFrameStage stage;
    CMKASTNodeId id;
    CMKEnv *env;
    CCBOOL owns_env;
    CMKASTNodeId next_child;
    CMKASTNodeId body;
    CMKValue iterable;
    const char *name;
    CCUINT32 name_len;
    CCUINT32 for_index;
    CCUINT32 for_count;
} CMKRtFrame;

static CCBOOL rt_expr_scan_user_call(
    CMKRuntime *runtime, CMKASTNodeId root, CCBOOL *has_call
)
{
    *has_call = CCFALSE;
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    if (!cmk_dyn_push(
            &stack, (CCUINT32)sizeof(CMKASTNodeId),
            (CCINT32)sizeof(CMKASTNodeId), &root
        ))
        return rt_error(runtime, root, "内存分配失败");

    while (stack.size >= (CCUINT32)sizeof(CMKASTNodeId))
    {
        CMKASTNodeId id = CMK_AST_NODE_NIL;
        if (cc_dyn_core_pop(
                &stack, (CCUINT32)sizeof(CMKASTNodeId),
                (CCINT32)sizeof(CMKASTNodeId), &id
            ) != 0)
        {
            ccalloc(stack.array, 0, NULL);
            return rt_error(runtime, root, "调用扫描栈损坏");
        }
        const CMKASTNode *node =
            cmk_ast_node(runtime->semantic->ast, id);
        if (!node)
        {
            ccalloc(stack.array, 0, NULL);
            return rt_error(runtime, id, "表达式节点缺失");
        }
        if (node->kind == CMK_AST_CALL &&
            cmk_semantic_node_function(runtime->semantic, id))
        {
            *has_call = CCTRUE;
            ccalloc(stack.array, 0, NULL);
            return CCTRUE;
        }
        for (CMKASTNodeId child = node->first_child;
             child != CMK_AST_NODE_NIL;)
        {
            if (!cmk_dyn_push(
                    &stack, (CCUINT32)sizeof(CMKASTNodeId),
                    (CCINT32)sizeof(CMKASTNodeId), &child
                ))
            {
                ccalloc(stack.array, 0, NULL);
                return rt_error(runtime, id, "内存分配失败");
            }
            const CMKASTNode *child_node =
                cmk_ast_node(runtime->semantic->ast, child);
            child = child_node
                ? child_node->next_sibling : CMK_AST_NODE_NIL;
        }
    }

    ccalloc(stack.array, 0, NULL);
    return CCTRUE;
}



typedef enum cmk_expr_insn_kind {
    CMK_EXPR_INS_VALUE = 0,
    CMK_EXPR_INS_UNARY,
    CMK_EXPR_INS_BINARY,
    CMK_EXPR_INS_INDEX,
    CMK_EXPR_INS_CALL
} CMKExprInsnKind;

typedef struct cmk_expr_insn {
    CMKExprInsnKind kind;
    CMKASTNodeId id;
    CMKUnaryOp unary;
    CMKBinaryOp binary;
    const CMKFunctionInfo *function;
    const CMKBuiltinSpec *builtin;
    CCUINT32 argc;
} CMKExprInsn;

struct cmk_expr_program {
    CCDyn insns;
    CCDyn values;
    CCUINT32 ip;
    CMKEnv *env;
    CCBOOL owns_env;
};

typedef struct cmk_prog_frame {
    CMKExprProgram program;
    const CMKFunctionInfo *function;
} CMKProgFrame;

static void rt_expr_program_clear(CMKExprProgram *program);



static void rt_call_args_clear(CMKValue *args, CCUINT32 argc);

static void rt_expr_program_clear(CMKExprProgram *program)
{
    if (!program)
        return;
    CMKValue *values = (CMKValue *)program->values.array;
    CCUINT32 count = program->values.size / (CCUINT32)sizeof(CMKValue);
    for (CCUINT32 i = 0; i < count; i++)
        cmk_value_clear(&values[i]);
    if (program->values.array)
        ccalloc(program->values.array, 0, NULL);
    if (program->insns.array)
        ccalloc(program->insns.array, 0, NULL);
    if (program->owns_env && program->env)
        env_destroy(program->env);
    memset(program, 0, sizeof(*program));
}

static CCBOOL rt_compile_expr(
    CMKRuntime *runtime, CMKASTNodeId id, CCDyn *insns
)
{
    CCBOOL has_call = CCFALSE;
    if (!rt_expr_scan_user_call(runtime, id, &has_call))
        return CCFALSE;
    if (!has_call)
    {
        CMKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = CMK_EXPR_INS_VALUE;
        insn.id = id;
        return cmk_dyn_push(
            insns, (CCUINT32)sizeof(CMKExprInsn),
            (CCINT32)sizeof(CMKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    const CMKASTNode *node = cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "表达式节点缺失");

    if (node->kind == CMK_AST_CALL)
    {
        CCUINT32 argc = 0;
        for (CMKASTNodeId arg = node->first_child;
             arg != CMK_AST_NODE_NIL;)
        {
            argc++;
            if (!rt_compile_expr(runtime, arg, insns))
                return CCFALSE;
            const CMKASTNode *arg_node =
                cmk_ast_node(runtime->semantic->ast, arg);
            arg = arg_node
                ? arg_node->next_sibling : CMK_AST_NODE_NIL;
        }
        CMKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = CMK_EXPR_INS_CALL;
        insn.id = id;
        insn.argc = argc;
        insn.function = cmk_semantic_node_function(
            runtime->semantic, id
        );
        insn.builtin = cmk_semantic_node_builtin(
            runtime->semantic, id
        );
        if (!insn.function && !insn.builtin)
            return rt_error(runtime, id, "调用未完成语义绑定");
        return cmk_dyn_push(
            insns, (CCUINT32)sizeof(CMKExprInsn),
            (CCINT32)sizeof(CMKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    if (node->kind == CMK_AST_UNARY)
    {
        CMKASTNodeId operand = node->first_child;
        if (!rt_compile_expr(runtime, operand, insns))
            return CCFALSE;
        CMKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = CMK_EXPR_INS_UNARY;
        insn.id = id;
        insn.unary = node->as.unary.op;
        return cmk_dyn_push(
            insns, (CCUINT32)sizeof(CMKExprInsn),
            (CCINT32)sizeof(CMKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    if (node->kind == CMK_AST_BINARY)
    {
        CMKASTNodeId left = node->first_child;
        const CMKASTNode *left_node =
            cmk_ast_node(runtime->semantic->ast, left);
        CMKASTNodeId right = left_node
            ? left_node->next_sibling : CMK_AST_NODE_NIL;
        if (!rt_compile_expr(runtime, left, insns) ||
            !rt_compile_expr(runtime, right, insns))
            return CCFALSE;
        CMKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = CMK_EXPR_INS_BINARY;
        insn.id = id;
        insn.binary = node->as.binary.op;
        return cmk_dyn_push(
            insns, (CCUINT32)sizeof(CMKExprInsn),
            (CCINT32)sizeof(CMKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    if (node->kind == CMK_AST_INDEX)
    {
        CMKASTNodeId base = node->first_child;
        const CMKASTNode *base_node =
            cmk_ast_node(runtime->semantic->ast, base);
        CMKASTNodeId index = base_node
            ? base_node->next_sibling : CMK_AST_NODE_NIL;
        if (!rt_compile_expr(runtime, base, insns) ||
            !rt_compile_expr(runtime, index, insns))
            return CCFALSE;
        CMKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = CMK_EXPR_INS_INDEX;
        insn.id = id;
        return cmk_dyn_push(
            insns, (CCUINT32)sizeof(CMKExprInsn),
            (CCINT32)sizeof(CMKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    CMKExprInsn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = CMK_EXPR_INS_VALUE;
    insn.id = id;
    return cmk_dyn_push(
        insns, (CCUINT32)sizeof(CMKExprInsn),
        (CCINT32)sizeof(CMKExprInsn), &insn
    ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
}

static CCBOOL rt_program_push(
    CMKRuntime *runtime, CMKExprProgram *program, CMKValue *value
)
{
    if (!cmk_dyn_push(
            &program->values, (CCUINT32)sizeof(CMKValue),
            (CCINT32)sizeof(CMKValue), value
        ))
        return rt_error(runtime, CMK_AST_NODE_NIL, "内存分配失败");
    cmk_value_init(value);
    return CCTRUE;
}

static CCBOOL rt_program_pop(
    CMKExprProgram *program, CMKValue *value
)
{
    return (cc_dyn_core_pop(
        &program->values, (CCUINT32)sizeof(CMKValue),
        (CCINT32)sizeof(CMKValue), value
    ) == 0) ? CCTRUE : CCFALSE;
}

static CCBOOL rt_expr_program_value(
    CMKRuntime *runtime, CMKExprProgram *program,
    const CMKExprInsn *insn
)
{
    CMKValue value;
    cmk_value_init(&value);
    if (!rt_eval_expr(runtime, insn->id, program->env, &value))
    {
        cmk_value_clear(&value);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &value))
    {
        cmk_value_clear(&value);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_unary(
    CMKRuntime *runtime, CMKExprProgram *program,
    const CMKExprInsn *insn
)
{
    CMKValue value;
    cmk_value_init(&value);
    if (!rt_program_pop(program, &value))
        return rt_error(runtime, insn->id, "表达式值栈损坏");
    CMKValue result;
    cmk_value_init(&result);
    CCBOOL ok = rt_apply_unary(
        runtime, insn->id, insn->unary, &value, &result
    );
    cmk_value_clear(&value);
    if (!ok)
    {
        cmk_value_clear(&result);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &result))
    {
        cmk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_binary(
    CMKRuntime *runtime, CMKExprProgram *program,
    const CMKExprInsn *insn
)
{
    CMKValue right;
    CMKValue left;
    cmk_value_init(&right);
    cmk_value_init(&left);
    if (!rt_program_pop(program, &right) ||
        !rt_program_pop(program, &left))
    {
        cmk_value_clear(&right);
        cmk_value_clear(&left);
        return rt_error(runtime, insn->id, "表达式值栈损坏");
    }
    CMKValue result;
    cmk_value_init(&result);
    CCBOOL ok = rt_apply_binary(
        runtime, insn->id, insn->binary, &left, &right, &result
    );
    cmk_value_clear(&left);
    cmk_value_clear(&right);
    if (!ok)
    {
        cmk_value_clear(&result);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &result))
    {
        cmk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_call(
    CMKRuntime *runtime, CMKExprProgram *program,
    const CMKExprInsn *insn, CCBOOL *suspended,
    const CMKFunctionInfo **call_function,
    CMKValue **call_args, CCUINT32 *call_argc
)
{
    CMKValue *args = NULL;
    if (insn->argc > 0)
    {
        args = ccalloc(
            NULL, (CCUINT64)insn->argc * sizeof(CMKValue),
            CMK_RT_COMMENT
        );
        if (!args)
            return rt_error(runtime, insn->id, "内存分配失败");
        for (CCUINT32 i = 0; i < insn->argc; i++)
            cmk_value_init(&args[i]);
        for (CCUINT32 i = insn->argc; i > 0; i--)
        {
            if (!rt_program_pop(program, &args[i - 1]))
            {
                rt_call_args_clear(args, insn->argc);
                return rt_error(
                    runtime, insn->id, "表达式值栈损坏"
                );
            }
        }
    }

    if (insn->function)
    {
        *suspended = CCTRUE;
        *call_function = insn->function;
        *call_args = args;
        *call_argc = insn->argc;
        return CCTRUE;
    }

    CMKValue result;
    cmk_value_init(&result);
    CCBOOL ok = rt_invoke_call(
        runtime, insn->id, NULL, insn->builtin,
        args, insn->argc, &result
    );
    rt_call_args_clear(args, insn->argc);
    if (!ok)
    {
        cmk_value_clear(&result);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &result))
    {
        cmk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_index(
    CMKRuntime *runtime, CMKExprProgram *program,
    const CMKExprInsn *insn
)
{
    CMKValue index;
    CMKValue base;
    cmk_value_init(&index);
    cmk_value_init(&base);
    if (!rt_program_pop(program, &index) ||
        !rt_program_pop(program, &base))
    {
        cmk_value_clear(&index);
        cmk_value_clear(&base);
        return rt_error(runtime, insn->id, "表达式值栈损坏");
    }
    if (base.type != CMK_VALUE_LIST ||
        index.type != CMK_VALUE_INT)
    {
        cmk_value_clear(&index);
        cmk_value_clear(&base);
        return rt_error(runtime, insn->id, "索引需要 list[int]");
    }
    CCINT64 at = index.as.integer;
    if (at < 0 || (CCUINT64)at >= cmk_list_count(base.as.list))
    {
        cmk_value_clear(&index);
        cmk_value_clear(&base);
        return rt_error(runtime, insn->id, "列表索引越界");
    }
    CMKValue result;
    cmk_value_init(&result);
    CCBOOL ok = cmk_value_copy(
        &result, cmk_list_at(base.as.list, (CCUINT32)at)
    );
    cmk_value_clear(&index);
    cmk_value_clear(&base);
    if (!ok)
        return rt_error(runtime, insn->id, "索引值复制失败");
    if (!rt_program_push(runtime, program, &result))
    {
        cmk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_run(
    CMKRuntime *runtime, CMKExprProgram *program, CMKValue *input,
    const CMKFunctionInfo **call_function, CMKValue **call_args,
    CCUINT32 *call_argc, CMKValue *out, CCBOOL *suspended
)
{
    *suspended = CCFALSE;
    if (input && !rt_program_push(runtime, program, input))
        return CCFALSE;

    CMKExprInsn *insns = (CMKExprInsn *)program->insns.array;
    CCUINT32 count = program->insns.size /
        (CCUINT32)sizeof(CMKExprInsn);
    while (program->ip < count)
    {
        CMKExprInsn *insn = &insns[program->ip++];
        CCBOOL ok = CCFALSE;
        switch (insn->kind)
        {
        case CMK_EXPR_INS_VALUE:
            ok = rt_expr_program_value(runtime, program, insn);
            break;
        case CMK_EXPR_INS_UNARY:
            ok = rt_expr_program_unary(runtime, program, insn);
            break;
        case CMK_EXPR_INS_BINARY:
            ok = rt_expr_program_binary(runtime, program, insn);
            break;
        case CMK_EXPR_INS_CALL:
            ok = rt_expr_program_call(
                runtime, program, insn, suspended,
                call_function, call_args, call_argc
            );
            break;
        case CMK_EXPR_INS_INDEX:
            ok = rt_expr_program_index(runtime, program, insn);
            break;
        default:
            return rt_error(runtime, insn->id, "未知表达式指令");
        }
        if (!ok)
            return CCFALSE;
        if (*suspended)
            return CCTRUE;
    }

    if (program->values.size != (CCUINT32)sizeof(CMKValue))
        return rt_error(runtime, CMK_AST_NODE_NIL, "表达式值栈不平衡");
    cmk_value_init(out);
    if (!rt_program_pop(program, out))
        return rt_error(runtime, CMK_AST_NODE_NIL, "表达式结果缺失");
    return CCTRUE;
}

static CCBOOL cmk_runtime_call_function_inner(
    CMKRuntime *runtime,
    const CMKFunctionInfo *function,
    const CMKValue *args, CCUINT32 argc,
    CMKValue *out
);

CCBOOL cmk_runtime_call_function(
    CMKRuntime *runtime,
    const CMKFunctionInfo *function,
    const CMKValue *args, CCUINT32 argc,
    CMKValue *out
)
{
    if (!runtime || !function || !out)
        return CCFALSE;
    if (!cmk_exec_stack_check((size_t)64 * 1024))
        return rt_error(
            runtime, function->decl, "执行栈空间不足"
        );
    if (runtime->policy.max_recursion_depth != 0 &&
        runtime->call_depth >= runtime->policy.max_recursion_depth)
        return rt_error(
            runtime, function->decl,
            "函数递归深度超过限制（%u）",
            runtime->policy.max_recursion_depth
        );
    runtime->call_depth++;
    CCBOOL ok = cmk_runtime_call_function_inner(
        runtime, function, args, argc, out
    );
    runtime->call_depth--;
    return ok;
}





static void rt_call_args_clear(CMKValue *args, CCUINT32 argc)
{
    if (!args)
        return;
    for (CCUINT32 i = 0; i < argc; i++)
        cmk_value_clear(&args[i]);
    ccalloc(args, 0, NULL);
}



typedef enum cmk_script_frame_kind {
    CMK_SCRIPT_BLOCK = 0,
    CMK_SCRIPT_IF,
    CMK_SCRIPT_WHILE,
    CMK_SCRIPT_FOR,
    CMK_SCRIPT_SIMPLE
} CMKScriptFrameKind;

typedef enum cmk_script_flow {
    CMK_SCRIPT_FLOW_NORMAL = 0,
    CMK_SCRIPT_FLOW_BREAK,
    CMK_SCRIPT_FLOW_CONTINUE
} CMKScriptFlow;

typedef struct cmk_script_frame {
    CMKScriptFrameKind kind;
    CMKASTNodeId id;
    CMKEnv *env;
    CCBOOL owns_env;
    CMKEnv *loop_env;
    CMKASTNodeId next;
    CCUINT32 stage;
    CMKASTNodeId body;
    CMKValue iterable;
    const char *name;
    CCUINT32 name_len;
    CCUINT32 index;
    CCUINT32 count;
} CMKScriptFrame;

typedef struct cmk_script {
    CMKRuntime *runtime;
    const CMKFunctionInfo *function;
    CMKEnv *env;
    CCDyn frames;
    CMKExecResult result;
    CMKExprProgram *pending;
    const CMKFunctionInfo *call_function;
    CMKValue *call_args;
    CCUINT32 call_argc;
    CMKScriptFlow flow;
    CCBOOL done;
} CMKScript;

static CMKScriptFrame *rt_script_top(CMKScript *script)
{
    if (!script || script->frames.size < (CCUINT32)sizeof(CMKScriptFrame))
        return NULL;
    return &((CMKScriptFrame *)script->frames.array)
        [script->frames.size / (CCUINT32)sizeof(CMKScriptFrame) - 1];
}

static void rt_script_frame_clear(CMKScriptFrame *frame)
{
    if (!frame)
        return;
    cmk_value_clear(&frame->iterable);
    if (frame->loop_env)
        env_destroy(frame->loop_env);
    if (frame->owns_env && frame->env)
        env_destroy(frame->env);
    memset(frame, 0, sizeof(*frame));
}

static void rt_script_clear(CMKScript *script)
{
    if (!script)
        return;
    CMKScriptFrame *frames = (CMKScriptFrame *)script->frames.array;
    CCUINT32 count = script->frames.size /
        (CCUINT32)sizeof(CMKScriptFrame);
    for (CCUINT32 i = 0; i < count; i++)
        rt_script_frame_clear(&frames[i]);
    if (script->frames.array)
        ccalloc(script->frames.array, 0, NULL);
    if (script->pending)
    {
        rt_expr_program_clear(script->pending);
        ccalloc(script->pending, 0, NULL);
        script->pending = NULL;
    }
    rt_call_args_clear(script->call_args, script->call_argc);
    script->call_args = NULL;
    script->call_argc = 0;
    exec_result_clear(&script->result);
    if (script->env)
        env_destroy(script->env);
    memset(script, 0, sizeof(*script));
}

static CCBOOL rt_script_push_frame(
    CMKRuntime *runtime, CMKScript *script,
    CMKScriptFrameKind kind, CMKASTNodeId id,
    CMKEnv *env, CCBOOL owns_env, CMKASTNodeId next
)
{
    CMKScriptFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = kind;
    frame.id = id;
    frame.env = env;
    frame.owns_env = owns_env;
    frame.next = next;
    if (!cmk_dyn_push(
            &script->frames, (CCUINT32)sizeof(CMKScriptFrame),
            (CCINT32)sizeof(CMKScriptFrame), &frame
        ))
        return rt_error(runtime, id, "内存分配失败");
    return CCTRUE;
}

typedef enum cmk_script_status {
    CMK_SCRIPT_RUN = 0,
    CMK_SCRIPT_WAIT,
    CMK_SCRIPT_DONE,
    CMK_SCRIPT_ERROR
} CMKScriptStatus;

static CMKASTNodeId rt_script_child(
    CMKRuntime *runtime, CMKASTNodeId id, CCUINT32 index
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
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

static CMKASTNodeId rt_script_var_init(
    CMKRuntime *runtime, CMKASTNodeId id
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return CMK_AST_NODE_NIL;
    CMKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < 1 && child != CMK_AST_NODE_NIL; i++)
    {
        const CMKASTNode *child_node =
            cmk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : CMK_AST_NODE_NIL;
    }
    return child;
}

static CMKASTNodeId rt_script_assign_value(
    CMKRuntime *runtime, CMKASTNodeId id
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return CMK_AST_NODE_NIL;
    CMKASTNodeId target = node->first_child;
    const CMKASTNode *target_node =
        cmk_ast_node(runtime->semantic->ast, target);
    return target_node ? target_node->next_sibling : CMK_AST_NODE_NIL;
}

static CCBOOL rt_script_push_statement(
    CMKRuntime *runtime, CMKScript *script,
    CMKASTNodeId id, CMKEnv *env
);

static CMKScriptStatus rt_script_start_expr(
    CMKScript *script, CMKASTNodeId expr_id,
    CMKEnv *env, CMKValue *out
)
{
    if (expr_id == CMK_AST_NODE_NIL)
    {
        cmk_value_init(out);
        return CMK_SCRIPT_RUN;
    }

    CCBOOL has_call = CCFALSE;
    if (!rt_expr_scan_user_call(script->runtime, expr_id, &has_call))
        return CMK_SCRIPT_ERROR;
    if (!has_call)
    {
        cmk_value_init(out);
        return rt_eval_expr(
            script->runtime, expr_id, env, out
        ) ? CMK_SCRIPT_RUN : CMK_SCRIPT_ERROR;
    }

    CMKExprProgram *program = ccalloc(
        NULL, sizeof(CMKExprProgram), CMK_RT_COMMENT
    );
    if (!program)
    {
        (void)rt_error(
            script->runtime, expr_id, "内存分配失败"
        );
        return CMK_SCRIPT_ERROR;
    }
    memset(program, 0, sizeof(*program));
    program->env = env;
    if (!rt_compile_expr(script->runtime, expr_id, &program->insns))
    {
        rt_expr_program_clear(program);
        ccalloc(program, 0, NULL);
        return CMK_SCRIPT_ERROR;
    }

    const CMKFunctionInfo *call_function = NULL;
    CMKValue *call_args = NULL;
    CCUINT32 call_argc = 0;
    CCBOOL suspended = CCFALSE;
    cmk_value_init(out);
    if (!rt_expr_program_run(
            script->runtime, program, NULL, &call_function,
            &call_args, &call_argc, out, &suspended
        ))
    {
        rt_expr_program_clear(program);
        ccalloc(program, 0, NULL);
        return CMK_SCRIPT_ERROR;
    }
    if (suspended)
    {
        script->pending = program;
        script->call_function = call_function;
        script->call_args = call_args;
        script->call_argc = call_argc;
        return CMK_SCRIPT_WAIT;
    }
    rt_expr_program_clear(program);
    ccalloc(program, 0, NULL);
    return CMK_SCRIPT_RUN;
}

static CMKScriptStatus rt_script_resume_expr(
    CMKScript *script, CMKValue *value, CMKValue *out
)
{
    if (!script->pending)
        return CMK_SCRIPT_ERROR;
    const CMKFunctionInfo *call_function = NULL;
    CMKValue *call_args = NULL;
    CCUINT32 call_argc = 0;
    CCBOOL suspended = CCFALSE;
    cmk_value_init(out);
    if (!rt_expr_program_run(
            script->runtime, script->pending, value,
            &call_function, &call_args, &call_argc, out, &suspended
        ))
        return CMK_SCRIPT_ERROR;
    if (suspended)
    {
        script->call_function = call_function;
        script->call_args = call_args;
        script->call_argc = call_argc;
        return CMK_SCRIPT_WAIT;
    }
    rt_expr_program_clear(script->pending);
    ccalloc(script->pending, 0, NULL);
    script->pending = NULL;
    return CMK_SCRIPT_RUN;
}

static CMKScriptStatus rt_script_apply_if(
    CMKScript *script, CMKValue *value
)
{
    if (value->type != CMK_VALUE_BOOL)
    {
        cmk_value_clear(value);
        (void)rt_error(
            script->runtime, rt_script_top(script)->id,
            "if 条件必须是 bool"
        );
        return CMK_SCRIPT_ERROR;
    }
    CCBOOL choose_then = value->as.boolean;
    cmk_value_clear(value);

    CMKScriptFrame *frame = rt_script_top(script);
    if (!frame)
        return CMK_SCRIPT_ERROR;
    CMKASTNodeId condition = rt_script_child(
        script->runtime, frame->id, 0
    );
    CMKASTNodeId then_id = CMK_AST_NODE_NIL;
    CMKASTNodeId else_id = CMK_AST_NODE_NIL;
    const CMKASTNode *condition_node =
        cmk_ast_node(script->runtime->semantic->ast, condition);
    if (condition_node)
    {
        then_id = condition_node->next_sibling;
        const CMKASTNode *then_node =
            cmk_ast_node(script->runtime->semantic->ast, then_id);
        else_id = then_node ? then_node->next_sibling : CMK_AST_NODE_NIL;
    }
    CMKASTNodeId branch = choose_then ? then_id : else_id;
    frame->stage = 1;
    if (branch == CMK_AST_NODE_NIL)
    {
        CMKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(CMKScriptFrame),
                (CCINT32)sizeof(CMKScriptFrame), &popped
            ) != 0)
            return CMK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
        return CMK_SCRIPT_RUN;
    }
    return rt_script_push_statement(
        script->runtime, script, branch, frame->env
    ) ? CMK_SCRIPT_RUN : CMK_SCRIPT_ERROR;
}

static CMKScriptStatus rt_script_apply_simple(
    CMKScript *script, CMKValue *value
)
{
    CMKScriptFrame *frame = rt_script_top(script);
    if (!frame)
    {
        cmk_value_clear(value);
        return CMK_SCRIPT_ERROR;
    }
    const CMKASTNode *node = cmk_ast_node(
        script->runtime->semantic->ast, frame->id
    );
    if (!node)
    {
        cmk_value_clear(value);
        return CMK_SCRIPT_ERROR;
    }

    CCBOOL ok = CCTRUE;
    if (node->kind == CMK_AST_VAR_DECL)
        ok = rt_var_decl_apply(
            script->runtime, frame->id, frame->env, value
        );
    else if (node->kind == CMK_AST_ASSIGN)
        ok = rt_assign_apply(
            script->runtime, frame->id, frame->env, value
        );
    else if (node->kind == CMK_AST_EXPR_STMT)
        cmk_value_clear(value);
    else if (node->kind == CMK_AST_RETURN)
    {
        script->result.kind = CMK_EXEC_RETURN;
        script->result.value = *value;
        cmk_value_init(value);
        script->done = CCTRUE;
    }
    else
    {
        cmk_value_clear(value);
        ok = rt_error(
            script->runtime, frame->id, "不支持的语句节点"
        );
    }
    if (!ok)
        return CMK_SCRIPT_ERROR;

    if (!script->done)
    {
        CMKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(CMKScriptFrame),
                (CCINT32)sizeof(CMKScriptFrame), &popped
            ) != 0)
            return CMK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
    }
    return CMK_SCRIPT_RUN;
}

static CMKScriptStatus rt_script_start_simple(CMKScript *script)
{
    CMKScriptFrame *frame = rt_script_top(script);
    if (!frame)
        return CMK_SCRIPT_ERROR;
    const CMKASTNode *node = cmk_ast_node(
        script->runtime->semantic->ast, frame->id
    );
    if (!node)
        return CMK_SCRIPT_ERROR;

    if (node->kind == CMK_AST_BREAK ||
        node->kind == CMK_AST_CONTINUE)
    {
        script->flow = (node->kind == CMK_AST_BREAK)
            ? CMK_SCRIPT_FLOW_BREAK : CMK_SCRIPT_FLOW_CONTINUE;
        CMKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(CMKScriptFrame),
                (CCINT32)sizeof(CMKScriptFrame), &popped
            ) != 0)
            return CMK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
        return CMK_SCRIPT_RUN;
    }

    CMKASTNodeId expr_id = CMK_AST_NODE_NIL;
    if (node->kind == CMK_AST_VAR_DECL)
        expr_id = rt_script_var_init(script->runtime, frame->id);
    else if (node->kind == CMK_AST_ASSIGN)
        expr_id = rt_script_assign_value(script->runtime, frame->id);
    else if (node->kind == CMK_AST_EXPR_STMT ||
             node->kind == CMK_AST_RETURN)
        expr_id = frame->id == CMK_AST_NODE_NIL
            ? CMK_AST_NODE_NIL : node->first_child;

    if (node->kind == CMK_AST_RETURN && expr_id == CMK_AST_NODE_NIL)
    {
        CMKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(CMKScriptFrame),
                (CCINT32)sizeof(CMKScriptFrame), &popped
            ) != 0)
            return CMK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
        script->result.kind = CMK_EXEC_NORMAL;
        script->done = CCTRUE;
        return CMK_SCRIPT_RUN;
    }

    if (expr_id == CMK_AST_NODE_NIL)
    {
        (void)rt_error(
            script->runtime, frame->id, "语句表达式缺失"
        );
        return CMK_SCRIPT_ERROR;
    }

    CCBOOL has_call = CCFALSE;
    if (!rt_expr_scan_user_call(
            script->runtime, expr_id, &has_call
        ))
        return CMK_SCRIPT_ERROR;
    if (!has_call)
    {
        CCBOOL ok = CCTRUE;
        if (node->kind == CMK_AST_VAR_DECL)
            ok = rt_eval_var_decl(
                script->runtime, frame->id, frame->env
            );
        else if (node->kind == CMK_AST_ASSIGN)
            ok = rt_eval_assign(
                script->runtime, frame->id, frame->env
            );
        else if (node->kind == CMK_AST_EXPR_STMT)
        {
            CMKValue value;
            cmk_value_init(&value);
            ok = rt_eval_expr(
                script->runtime, expr_id, frame->env, &value
            );
            cmk_value_clear(&value);
        }
        else if (node->kind == CMK_AST_RETURN)
        {
            CMKValue value;
            cmk_value_init(&value);
            ok = rt_eval_expr(
                script->runtime, expr_id, frame->env, &value
            );
            if (ok)
            {
                script->result.kind = CMK_EXEC_RETURN;
                script->result.value = value;
                cmk_value_init(&value);
                script->done = CCTRUE;
            }
            cmk_value_clear(&value);
        }
        if (!ok)
            return CMK_SCRIPT_ERROR;
        if (!script->done)
        {
            CMKScriptFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (cc_dyn_core_pop(
                    &script->frames, (CCUINT32)sizeof(CMKScriptFrame),
                    (CCINT32)sizeof(CMKScriptFrame), &popped
                ) != 0)
                return CMK_SCRIPT_ERROR;
            rt_script_frame_clear(&popped);
        }
        return CMK_SCRIPT_RUN;
    }

    frame->stage = 1;
    CMKValue value;
    CMKScriptStatus status = rt_script_start_expr(
        script, expr_id, frame->env, &value
    );
    if (status != CMK_SCRIPT_RUN)
        return status;
    return rt_script_apply_simple(script, &value);
}

static CMKScriptStatus rt_script_pop_top(CMKScript *script)
{
    CMKScriptFrame popped;
    memset(&popped, 0, sizeof(popped));
    if (cc_dyn_core_pop(
            &script->frames, (CCUINT32)sizeof(CMKScriptFrame),
            (CCINT32)sizeof(CMKScriptFrame), &popped
        ) != 0)
        return CMK_SCRIPT_ERROR;
    rt_script_frame_clear(&popped);
    return CMK_SCRIPT_RUN;
}

static CMKScriptStatus rt_script_apply_while(
    CMKScript *script, CMKValue *value
)
{
    if (value->type != CMK_VALUE_BOOL)
    {
        cmk_value_clear(value);
        (void)rt_error(
            script->runtime, rt_script_top(script)->id,
            "while 条件必须是 bool"
        );
        return CMK_SCRIPT_ERROR;
    }
    CCBOOL run = value->as.boolean;
    cmk_value_clear(value);
    CMKScriptFrame *frame = rt_script_top(script);
    if (!frame)
        return CMK_SCRIPT_ERROR;
    if (!run)
        return rt_script_pop_top(script);
    if (frame->body == CMK_AST_NODE_NIL)
    {
        (void)rt_error(
            script->runtime, frame->id, "while 循环体缺失"
        );
        return CMK_SCRIPT_ERROR;
    }
    frame->stage = 1;
    CMKASTNodeId body = frame->body;
    CMKEnv *env = frame->env;
    return rt_script_push_statement(
        script->runtime, script, body, env
    ) ? CMK_SCRIPT_RUN : CMK_SCRIPT_ERROR;
}

static CMKScriptStatus rt_script_for_next(CMKScript *script)
{
    CMKScriptFrame *frame = rt_script_top(script);
    if (!frame || frame->kind != CMK_SCRIPT_FOR)
        return CMK_SCRIPT_ERROR;
    if (frame->index >= frame->count)
        return rt_script_pop_top(script);

    const CMKValue *item = cmk_list_at(
        frame->iterable.as.list, frame->index
    );
    if (!item)
    {
        (void)rt_error(
            script->runtime, frame->id, "for-in 元素缺失"
        );
        return CMK_SCRIPT_ERROR;
    }
    CMKEnvVar *slot = env_slot(
        frame->loop_env, frame->name, frame->name_len
    );
    if (!slot)
    {
        if (!env_define(
                script->runtime, frame->loop_env,
                frame->name, frame->name_len, item,
                CCFALSE, frame->id
            ))
            return CMK_SCRIPT_ERROR;
    }
    else if (!cmk_value_copy(&slot->value, item))
    {
        (void)rt_error(
            script->runtime, frame->id, "循环变量赋值失败"
        );
        return CMK_SCRIPT_ERROR;
    }
    if (frame->body == CMK_AST_NODE_NIL)
    {
        (void)rt_error(
            script->runtime, frame->id, "for-in 循环体缺失"
        );
        return CMK_SCRIPT_ERROR;
    }
    frame->stage = 2;
    CMKASTNodeId body = frame->body;
    CMKEnv *env = frame->loop_env;
    return rt_script_push_statement(
        script->runtime, script, body, env
    ) ? CMK_SCRIPT_RUN : CMK_SCRIPT_ERROR;
}

static CMKScriptStatus rt_script_apply_for(
    CMKScript *script, CMKValue *value
)
{
    CMKScriptFrame *frame = rt_script_top(script);
    if (!frame)
    {
        cmk_value_clear(value);
        return CMK_SCRIPT_ERROR;
    }
    const CMKASTNode *node = cmk_ast_node(
        script->runtime->semantic->ast, frame->id
    );
    if (!node)
    {
        cmk_value_clear(value);
        return CMK_SCRIPT_ERROR;
    }
    if (value->type != CMK_VALUE_LIST)
    {
        cmk_value_clear(value);
        (void)rt_error(
            script->runtime, frame->id, "for-in 需要 list"
        );
        return CMK_SCRIPT_ERROR;
    }
    CCUINT32 name_len = 0;
    const char *name = cmk_ast_string(
        script->runtime->semantic->ast,
        node->as.for_in.name, &name_len
    );
    if (!name)
    {
        cmk_value_clear(value);
        (void)rt_error(
            script->runtime, frame->id, "for-in 变量名无效"
        );
        return CMK_SCRIPT_ERROR;
    }
    frame->loop_env = env_create(frame->env);
    if (!frame->loop_env)
    {
        cmk_value_clear(value);
        (void)rt_error(
            script->runtime, frame->id, "内存分配失败"
        );
        return CMK_SCRIPT_ERROR;
    }
    frame->iterable = *value;
    cmk_value_init(value);
    frame->name = name;
    frame->name_len = name_len;
    frame->count = cmk_list_count(frame->iterable.as.list);
    frame->index = 0;
    frame->stage = 1;

    CMKASTNodeId type_id = node->first_child;
    const CMKASTNode *type_node =
        cmk_ast_node(script->runtime->semantic->ast, type_id);
    CMKASTNodeId iterable_id = type_node
        ? type_node->next_sibling : CMK_AST_NODE_NIL;
    const CMKASTNode *iterable_node =
        cmk_ast_node(script->runtime->semantic->ast, iterable_id);
    frame->body = iterable_node
        ? iterable_node->next_sibling : CMK_AST_NODE_NIL;
    return rt_script_for_next(script);
}

static CMKScriptStatus rt_script_apply_value(
    CMKScript *script, CMKValue *value
)
{
    CMKScriptFrame *frame = rt_script_top(script);
    if (!frame)
    {
        cmk_value_clear(value);
        return CMK_SCRIPT_ERROR;
    }
    if (frame->kind == CMK_SCRIPT_IF)
        return rt_script_apply_if(script, value);
    if (frame->kind == CMK_SCRIPT_WHILE)
        return rt_script_apply_while(script, value);
    if (frame->kind == CMK_SCRIPT_FOR)
        return rt_script_apply_for(script, value);
    if (frame->kind == CMK_SCRIPT_SIMPLE)
        return rt_script_apply_simple(script, value);
    cmk_value_clear(value);
    (void)rt_error(
        script->runtime, frame->id, "脚本帧类型错误"
    );
    return CMK_SCRIPT_ERROR;
}

static CCBOOL rt_script_push_statement(
    CMKRuntime *runtime, CMKScript *script,
    CMKASTNodeId id, CMKEnv *env
)
{
    const CMKASTNode *node =
        cmk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "语句节点缺失");
    if (node->kind == CMK_AST_BLOCK)
    {
        CMKEnv *block_env = env_create(env);
        if (!block_env)
            return rt_error(runtime, id, "内存分配失败");
        return rt_script_push_frame(
            runtime, script, CMK_SCRIPT_BLOCK, id,
            block_env, CCTRUE, node->first_child
        );
    }
    if (node->kind == CMK_AST_IF)
        return rt_script_push_frame(
            runtime, script, CMK_SCRIPT_IF, id,
            env, CCFALSE, CMK_AST_NODE_NIL
        );
    if (node->kind == CMK_AST_WHILE)
    {
        CMKASTNodeId condition = node->first_child;
        const CMKASTNode *condition_node =
            cmk_ast_node(runtime->semantic->ast, condition);
        CMKASTNodeId body = condition_node
            ? condition_node->next_sibling : CMK_AST_NODE_NIL;
        if (!rt_script_push_frame(
                runtime, script, CMK_SCRIPT_WHILE, id,
                env, CCFALSE, CMK_AST_NODE_NIL
            ))
            return CCFALSE;
        CMKScriptFrame *frame = rt_script_top(script);
        if (!frame)
            return rt_error(runtime, id, "脚本帧缺失");
        frame->body = body;
        return CCTRUE;
    }
    if (node->kind == CMK_AST_FOR_IN)
        return rt_script_push_frame(
            runtime, script, CMK_SCRIPT_FOR, id,
            env, CCFALSE, CMK_AST_NODE_NIL
        );
    if (node->kind == CMK_AST_VAR_DECL ||
        node->kind == CMK_AST_ASSIGN ||
        node->kind == CMK_AST_EXPR_STMT ||
        node->kind == CMK_AST_RETURN ||
        node->kind == CMK_AST_BREAK ||
        node->kind == CMK_AST_CONTINUE)
        return rt_script_push_frame(
            runtime, script, CMK_SCRIPT_SIMPLE, id,
            env, CCFALSE, CMK_AST_NODE_NIL
        );
    return rt_error(runtime, id, "不支持的语句节点");
}

static CMKScriptStatus rt_script_run(CMKScript *script, CMKValue *input)
{
    if (script->done)
        return CMK_SCRIPT_DONE;
    if (script->pending)
    {
        if (!input)
        {
            (void)rt_error(
                script->runtime, script->function->decl,
                "脚本缺少恢复值：%s", script->function->name.data
            );
            return CMK_SCRIPT_ERROR;
        }
        CMKValue value;
        CMKScriptStatus status = rt_script_resume_expr(
            script, input, &value
        );
        if (status != CMK_SCRIPT_RUN)
            return status;
        status = rt_script_apply_value(script, &value);
        if (status != CMK_SCRIPT_RUN)
            return status;
    }

    for (;;)
    {
        if (script->done)
            return CMK_SCRIPT_DONE;
        CMKScriptFrame *frame = rt_script_top(script);
        if (!frame)
        {
            script->result.kind = CMK_EXEC_NORMAL;
            script->done = CCTRUE;
            return CMK_SCRIPT_DONE;
        }

        if (script->flow != CMK_SCRIPT_FLOW_NORMAL)
        {
            while (frame && frame->kind != CMK_SCRIPT_WHILE &&
                   frame->kind != CMK_SCRIPT_FOR)
            {
                if (rt_script_pop_top(script) != CMK_SCRIPT_RUN)
                    return CMK_SCRIPT_ERROR;
                frame = rt_script_top(script);
            }
            if (!frame)
            {
                (void)rt_error(
                    script->runtime, script->function->decl,
                    "break/continue 不在循环内"
                );
                return CMK_SCRIPT_ERROR;
            }
            if (script->flow == CMK_SCRIPT_FLOW_BREAK)
            {
                script->flow = CMK_SCRIPT_FLOW_NORMAL;
                if (rt_script_pop_top(script) != CMK_SCRIPT_RUN)
                    return CMK_SCRIPT_ERROR;
                continue;
            }
            frame->stage = (frame->kind == CMK_SCRIPT_WHILE) ? 0 : 2;
            script->flow = CMK_SCRIPT_FLOW_NORMAL;
            continue;
        }

        if (frame->kind == CMK_SCRIPT_BLOCK)
        {
            if (frame->next == CMK_AST_NODE_NIL)
            {
                CMKScriptFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (cc_dyn_core_pop(
                        &script->frames,
                        (CCUINT32)sizeof(CMKScriptFrame),
                        (CCINT32)sizeof(CMKScriptFrame), &popped
                    ) != 0)
                    return CMK_SCRIPT_ERROR;
                rt_script_frame_clear(&popped);
                continue;
            }
            CMKASTNodeId child = frame->next;
            const CMKASTNode *child_node = cmk_ast_node(
                script->runtime->semantic->ast, child
            );
            frame->next = child_node
                ? child_node->next_sibling : CMK_AST_NODE_NIL;
            if (!rt_script_push_statement(
                    script->runtime, script, child, frame->env
                ))
                return CMK_SCRIPT_ERROR;
            continue;
        }

        if (frame->kind == CMK_SCRIPT_WHILE)
        {
            if (frame->stage == 1)
            {
                frame->stage = 0;
                continue;
            }
            CMKASTNodeId condition = rt_script_child(
                script->runtime, frame->id, 0
            );
            CMKValue value;
            CMKScriptStatus status = rt_script_start_expr(
                script, condition, frame->env, &value
            );
            if (status != CMK_SCRIPT_RUN)
                return status;
            status = rt_script_apply_while(script, &value);
            if (status != CMK_SCRIPT_RUN)
                return status;
            continue;
        }

        if (frame->kind == CMK_SCRIPT_FOR)
        {
            if (frame->stage == 2)
            {
                frame->index++;
                frame->stage = 1;
                continue;
            }
            if (frame->stage == 1)
            {
                CMKScriptStatus status = rt_script_for_next(script);
                if (status != CMK_SCRIPT_RUN)
                    return status;
                continue;
            }
            CMKASTNodeId type_id = rt_script_child(
                script->runtime, frame->id, 0
            );
            CMKASTNodeId iterable_id = rt_script_child(
                script->runtime, frame->id, 1
            );
            (void)type_id;
            CMKValue value;
            CMKScriptStatus status = rt_script_start_expr(
                script, iterable_id, frame->env, &value
            );
            if (status != CMK_SCRIPT_RUN)
                return status;
            status = rt_script_apply_for(script, &value);
            if (status != CMK_SCRIPT_RUN)
                return status;
            continue;
        }

        if (frame->kind == CMK_SCRIPT_IF)
        {
            if (frame->stage == 1)
            {
                CMKScriptFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (cc_dyn_core_pop(
                        &script->frames,
                        (CCUINT32)sizeof(CMKScriptFrame),
                        (CCINT32)sizeof(CMKScriptFrame), &popped
                    ) != 0)
                    return CMK_SCRIPT_ERROR;
                rt_script_frame_clear(&popped);
                continue;
            }
            CMKASTNodeId condition = rt_script_child(
                script->runtime, frame->id, 0
            );
            CMKValue value;
            CMKScriptStatus status = rt_script_start_expr(
                script, condition, frame->env, &value
            );
            if (status != CMK_SCRIPT_RUN)
            {
                if (status == CMK_SCRIPT_WAIT)
                    frame->stage = 1;
                return status;
            }
            status = rt_script_apply_if(script, &value);
            if (status != CMK_SCRIPT_RUN)
                return status;
            continue;
        }

        CMKScriptStatus status = rt_script_start_simple(script);
        if (status != CMK_SCRIPT_RUN)
            return status;
    }
}

static CMKScript *rt_script_create(
    CMKRuntime *runtime, const CMKFunctionInfo *function,
    const CMKValue *args, CCUINT32 argc
)
{
    if (argc != function->param_count)
    {
        (void)rt_error(
            runtime, function->decl, "函数 %s 参数数量错误",
            function->name.data
        );
        return NULL;
    }

    CMKScript *script = ccalloc(
        NULL, sizeof(CMKScript), CMK_RT_COMMENT
    );
    if (!script)
    {
        (void)rt_error(runtime, function->decl, "内存分配失败");
        return NULL;
    }
    memset(script, 0, sizeof(*script));
    script->runtime = runtime;
    script->function = function;
    exec_result_init(&script->result);

    script->env = env_create(runtime->module_env);
    if (!script->env)
    {
        rt_script_clear(script);
        ccalloc(script, 0, NULL);
        (void)rt_error(runtime, function->decl, "内存分配失败");
        return NULL;
    }

    const CMKASTNode *decl =
        cmk_ast_node(runtime->semantic->ast, function->decl);
    if (!decl)
    {
        rt_script_clear(script);
        ccalloc(script, 0, NULL);
        (void)rt_error(runtime, function->decl, "函数声明缺失");
        return NULL;
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
            rt_script_clear(script);
            ccalloc(script, 0, NULL);
            (void)rt_error(
                runtime, function->decl,
                "函数参数缺失：%s", function->name.data
            );
            return NULL;
        }
        if (!rt_expect_value(
                runtime, param, &args[i], &function->params[i]
            ))
        {
            rt_script_clear(script);
            ccalloc(script, 0, NULL);
            return NULL;
        }
        CCUINT32 name_len = 0;
        const char *name = cmk_ast_string(
            runtime->semantic->ast, param_node->as.param.name,
            &name_len
        );
        if (!name || !env_define(
                runtime, script->env, name, name_len,
                &args[i], CCFALSE, param
            ))
        {
            rt_script_clear(script);
            ccalloc(script, 0, NULL);
            return NULL;
        }
        const CMKASTNode *next = cmk_ast_node(
            runtime->semantic->ast, param_node->next_sibling
        );
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
    const CMKASTNode *body_node =
        cmk_ast_node(runtime->semantic->ast, body);
    if (!body_node)
    {
        rt_script_clear(script);
        ccalloc(script, 0, NULL);
        (void)rt_error(
            runtime, body, "函数体缺失：%s", function->name.data
        );
        return NULL;
    }
    if (!rt_script_push_frame(
            runtime, script, CMK_SCRIPT_BLOCK, body,
            script->env, CCFALSE, body_node->first_child
        ))
    {
        rt_script_clear(script);
        ccalloc(script, 0, NULL);
        return NULL;
    }
    return script;
}



/* 处理脚本挂起的函数调用；*pushed 表示是否压入子脚本。 */
static CCBOOL rt_script_start_call(
    CMKRuntime *runtime, CMKScript *script,
    CCDyn *scripts, CCBOOL *pushed
)
{
    *pushed = CCFALSE;
    if (!script->call_args)
        return CCTRUE;

    const CMKFunctionInfo *callee = script->call_function;
    CMKValue *call_args = script->call_args;
    CCUINT32 call_argc = script->call_argc;
    script->call_args = NULL;
    script->call_argc = 0;

    if (!callee)
    {
        rt_call_args_clear(call_args, call_argc);
        (void)rt_error(
            runtime, script->function->decl, "调用目标缺失"
        );
        return CCFALSE;
    }

    CMKRuntime *target = runtime;
    if (callee->module != runtime->semantic)
    {
        target = runtime->module_resolver
            ? runtime->module_resolver(runtime, callee->module)
            : NULL;
        if (!target || target == runtime)
        {
            rt_call_args_clear(call_args, call_argc);
            (void)rt_error(
                runtime, callee->decl,
                "跨模块函数调用缺少运行时"
            );
            return CCFALSE;
        }
        if (!cmk_runtime_init_globals(target))
        {
            rt_call_args_clear(call_args, call_argc);
            return CCFALSE;
        }
    }

    if (target->policy.max_recursion_depth != 0 &&
        target->call_depth >= target->policy.max_recursion_depth)
    {
        rt_call_args_clear(call_args, call_argc);
        (void)rt_error(
            target, callee->decl,
            "函数递归深度超过限制（%u）",
            target->policy.max_recursion_depth
        );
        return CCFALSE;
    }

    CMKScript *child = rt_script_create(
        target, callee, call_args, call_argc
    );
    rt_call_args_clear(call_args, call_argc);
    if (!child)
        return CCFALSE;
    target->call_depth++;
    if (!cmk_dyn_push(
            scripts, (CCUINT32)sizeof(CMKScript *),
            (CCINT32)sizeof(CMKScript *), &child
        ))
    {
        target->call_depth--;
        rt_script_clear(child);
        ccalloc(child, 0, NULL);
        (void)rt_error(runtime, callee->decl, "内存分配失败");
        return CCFALSE;
    }
    *pushed = CCTRUE;
    return CCTRUE;
}

static CCBOOL rt_script_execute(
    CMKRuntime *runtime, const CMKFunctionInfo *function,
    const CMKValue *args, CCUINT32 argc, CMKValue *out
)
{
    CCDyn scripts;
    memset(&scripts, 0, sizeof(scripts));
    CMKScript *root = rt_script_create(
        runtime, function, args, argc
    );
    if (!root)
        return CCFALSE;
    if (!cmk_dyn_push(
            &scripts, (CCUINT32)sizeof(CMKScript *),
            (CCINT32)sizeof(CMKScript *), &root
        ))
    {
        rt_script_clear(root);
        ccalloc(root, 0, NULL);
        return rt_error(runtime, function->decl, "内存分配失败");
    }

    CCBOOL ok = CCFALSE;
    CCBOOL have_value = CCFALSE;
    CMKValue value;
    cmk_value_init(&value);

    for (;;)
    {
        CCUINT32 count = scripts.size /
            (CCUINT32)sizeof(CMKScript *);
        CMKScript **items = (CMKScript **)scripts.array;
        CMKScript *script = items[count - 1];

        CCBOOL pushed = CCFALSE;
        if (!rt_script_start_call(runtime, script, &scripts, &pushed))
            goto Finished;
        if (pushed)
            continue;

        CMKScriptStatus status = rt_script_run(script, NULL);
        if (status == CMK_SCRIPT_ERROR)
        {
            if (!cmk_diagnostic_has(runtime->diagnostic))
                (void)rt_error(
                    runtime, script->function->decl,
                    "脚本执行失败：%s",
                    script->function->name.data
                );
            goto Finished;
        }
        if (status == CMK_SCRIPT_WAIT)
            continue;

        /* 脚本完成：校验收敛并回传调用者 */
        if (script->result.kind == CMK_EXEC_RETURN)
        {
            if (!rt_expect_value(
                    runtime, script->function->decl,
                    &script->result.value,
                    &script->function->return_type
                ))
                goto Finished;
            cmk_value_clear(&value);
            value = script->result.value;
            cmk_value_init(&script->result.value);
        }
        else if (script->result.kind == CMK_EXEC_NORMAL)
        {
            if (script->function->return_type.kind != CMK_VALUE_VOID)
            {
                (void)rt_error(
                    runtime, script->function->decl,
                    "非 void 函数 %s 没有返回值",
                    script->function->name.data
                );
                goto Finished;
            }
            cmk_value_clear(&value);
            (void)cmk_value_set_void(&value);
        }
        else
        {
            (void)rt_error(
                runtime, script->function->decl,
                "函数 %s 控制流异常",
                script->function->name.data
            );
            goto Finished;
        }
        have_value = CCTRUE;

        CMKRuntime *script_runtime = script->runtime;
        rt_script_clear(script);
        ccalloc(script, 0, NULL);
        scripts.size -= (CCUINT32)sizeof(CMKScript *);
        script_runtime->call_depth--;

        count = scripts.size / (CCUINT32)sizeof(CMKScript *);
        if (count == 0)
        {
            *out = value;
            cmk_value_init(&value);
            ok = CCTRUE;
            goto Finished;
        }

        CMKScript *caller = ((CMKScript **)scripts.array)[count - 1];
        CMKValue input = value;
        cmk_value_init(&value);
        status = rt_script_run(caller, &input);
        if (status == CMK_SCRIPT_ERROR)
            goto Finished;
        have_value = CCFALSE;
    }

Finished:
    {
        CCUINT32 count = scripts.size / (CCUINT32)sizeof(CMKScript *);
        CMKScript **items = (CMKScript **)scripts.array;
        for (CCUINT32 i = 0; i < count; i++)
        {
            rt_script_clear(items[i]);
            ccalloc(items[i], 0, NULL);
        }
        if (scripts.array)
            ccalloc(scripts.array, 0, NULL);
    }
    cmk_value_clear(&value);
    (void)have_value;
    return ok;
}

/* ============================================================================
 * 模块配置
 * ========================================================================== */

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

static CCBOOL cmk_runtime_call_function_inner(
    CMKRuntime *runtime,
    const CMKFunctionInfo *function,
    const CMKValue *args, CCUINT32 argc,
    CMKValue *out
)
{
    if (function->module != runtime->semantic)
    {
        CMKRuntime *target = runtime->module_resolver
            ? runtime->module_resolver(runtime, function->module)
            : NULL;
        if (!target || target == runtime)
            return rt_error(
                runtime, function->decl,
                "跨模块函数调用缺少运行时：%s",
                function->name.data
            );
        if (!cmk_runtime_init_globals(target))
            return CCFALSE;
        return cmk_runtime_call_function(
            target, function, args, argc, out
        );
    }
    return rt_script_execute(runtime, function, args, argc, out);
}

static CCBOOL cmk_runtime_configure_body(CMKRuntime *runtime)
{
    if (!runtime || !runtime->semantic)
        return CCFALSE;
    if (runtime->configured)
        return runtime->failed ? CCFALSE : CCTRUE;

    runtime->configure_start = clock();
    runtime->timer_started =
        (runtime->configure_start == (clock_t)-1) ? CCFALSE : CCTRUE;

    if (!cmk_runtime_init_globals(runtime))
    {
        runtime->configured = CCTRUE;
        return CCFALSE;
    }
    runtime->configured = CCTRUE;

    return CCTRUE;
}

typedef struct cmk_configure_stack_ctx {
    CMKRuntime *runtime;
} CMKConfigureStackCtx;

static CCBOOL cmk_configure_stack_entry(void *user)
{
    CMKConfigureStackCtx *ctx = (CMKConfigureStackCtx *)user;
    return cmk_runtime_configure_body(ctx->runtime);
}

CCBOOL cmk_runtime_configure(CMKRuntime *runtime)
{
    if (!runtime || !runtime->semantic)
        return CCFALSE;
    if (runtime->configured)
        return runtime->failed ? CCFALSE : CCTRUE;

    CMKConfigureStackCtx ctx;
    ctx.runtime = runtime;
    CCBOOL result = CCFALSE;
    if (!cmk_exec_stack_run(
            cmk_configure_stack_entry, &ctx, &result
        ))
        return rt_error(
            runtime, CMK_AST_NODE_NIL, "执行栈分配失败"
        );
    return result;
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

void cmk_runtime_set_policy(
    CMKRuntime *runtime, const CMKExecutionPolicy *policy
)
{
    if (!runtime || !policy)
        return;
    runtime->policy = *policy;
}

const CMKExecutionPolicy *cmk_runtime_get_policy(
    const CMKRuntime *runtime
)
{
    return runtime ? &runtime->policy : NULL;
}

static CCBOOL rt_step(CMKRuntime *runtime, CMKASTNodeId node)
{
    if (!runtime)
        return CCFALSE;
    if (runtime->policy.max_configure_time_ms != 0 &&
        runtime->timer_started)
    {
        clock_t now = clock();
        if (now != (clock_t)-1)
        {
            CCUINT64 elapsed_ticks = (CCUINT64)(now - runtime->configure_start);
            CCUINT64 elapsed_ms = (elapsed_ticks * 1000) /
                (CCUINT64)CLOCKS_PER_SEC;
            if (elapsed_ms > runtime->policy.max_configure_time_ms)
                return rt_error(
                    runtime, node, "Configure 执行时间超过限制（%llu ms）",
                    (unsigned long long)
                        runtime->policy.max_configure_time_ms
                );
        }
    }
    if (runtime->policy.max_steps == 0)
        return CCTRUE;
    runtime->steps++;
    if (runtime->steps > runtime->policy.max_steps)
        return rt_error(
            runtime, node, "执行步数超过限制（%llu）",
            (unsigned long long)runtime->policy.max_steps
        );
    return CCTRUE;
}

CCBOOL cmk_runtime_is_configured(const CMKRuntime *runtime)
{
    return (runtime && runtime->configured) ? CCTRUE : CCFALSE;
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
    if (runtime->module_env)
        env_destroy(runtime->module_env);
    ccalloc(runtime, 0, NULL);
}
