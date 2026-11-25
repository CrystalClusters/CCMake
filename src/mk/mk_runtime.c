/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK Configure 求值运行时
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_runtime.h"
#include "mk_semantic_internal.h"
#include "mk_safe.h"
#include "mk_exec_stack.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MK_RT_COMMENT CC_DBG_REL_TAG(".mk 运行时", NULL)

#define MK_INT64_MAX_ ((CCINT64)0x7FFFFFFFFFFFFFFF)
#define MK_INT64_MIN_ (-(MK_INT64_MAX_) - 1)

typedef struct mk_env_var {
    MKString name;
    MKValue value;
    CCBOOL is_const;
} MKEnvVar;

typedef struct mk_env {
    struct mk_env *parent;
    CCDyn vars;   /* MKEnvVar[] */
} MKEnv;

typedef enum mk_exec_kind {
    MK_EXEC_NORMAL = 0,
    MK_EXEC_RETURN,
    MK_EXEC_BREAK,
    MK_EXEC_CONTINUE,
    MK_EXEC_ERROR,
    MK_EXEC_TAILCALL,
    MK_EXEC_DEFER,
    MK_EXEC_EXPR_DEFER
} MKExecKind;

typedef struct mk_expr_program MKExprProgram;

typedef enum mk_continuation_kind {
    MK_CONT_BINARY = 0,
    MK_CONT_UNARY
} MKContinuationKind;

typedef struct mk_continuation {
    MKContinuationKind kind;
    MKBinaryOp binary;
    MKUnaryOp unary;
    MKValue operand;
} MKContinuation;

typedef struct mk_exec_result {
    MKExecKind kind;
    MKValue value;
    const MKFunctionInfo *tail_function;
    MKValue *tail_args;
    CCUINT32 tail_argc;
    const MKFunctionInfo *defer_function;
    MKValue *defer_args;
    CCUINT32 defer_argc;
    CCDyn continuations;
    MKExprProgram *expr_program;
    const MKFunctionInfo *expr_function;
} MKExecResult;

typedef struct mk_cont_frame {
    CCDyn cont;
} MKContFrame;

struct mk_runtime {
    MKSemanticModule *semantic;
    MKConfig *config;
    MKBuiltinRegistry *builtins;
    MKDiagnostic *diagnostic;
    MKEnv *module_env;
    CCDyn globals;
    MKRuntimeModuleResolver module_resolver;
    void *resolver_user;
    const char *work_dir;
    MKExecutionPolicy policy;
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

static CCBOOL rt_step(MKRuntime *runtime, MKASTNodeId node);

static CCBOOL rt_error(
    MKRuntime *runtime, MKASTNodeId node, const char *fmt, ...
)
{    if (!runtime || !runtime->diagnostic)
        return CCFALSE;
    runtime->failed = CCTRUE;
    if (mk_diagnostic_has(runtime->diagnostic))
        return CCFALSE;

    const MKASTNode *ast_node = NULL;
    const char *file = runtime->semantic ? runtime->semantic->path : "(.mk)";
    CCUINT32 line = 0;
    CCUINT32 col = 0;
    if (runtime->semantic && runtime->semantic->ast)
    {
        ast_node = mk_ast_node(runtime->semantic->ast, node);
        if (ast_node)
        {
            file = mk_ast_file(runtime->semantic->ast);
            line = ast_node->span.line;
            col = ast_node->span.col;
        }
    }

    va_list ap;
    va_start(ap, fmt);
    (void)mk_diagnostic_vset(
        runtime->diagnostic, MK_DIAG_ERROR, file, line, col, fmt, ap
    );
    va_end(ap);
    return CCFALSE;
}

static void exec_result_init(MKExecResult *result)
{
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    result->kind = MK_EXEC_NORMAL;
    mk_value_init(&result->value);
}

static void rt_expr_program_clear(MKExprProgram *program);

static void exec_result_clear(MKExecResult *result)
{
    if (!result)
        return;
    mk_value_clear(&result->value);
    if (result->tail_args)
    {
        for (CCUINT32 i = 0; i < result->tail_argc; i++)
            mk_value_clear(&result->tail_args[i]);
        ccalloc(result->tail_args, 0, NULL);
    }
    if (result->defer_args)
    {
        for (CCUINT32 i = 0; i < result->defer_argc; i++)
            mk_value_clear(&result->defer_args[i]);
        ccalloc(result->defer_args, 0, NULL);
    }
    MKContinuation *conts = (MKContinuation *)result->continuations.array;
    CCUINT32 cont_count = result->continuations.size /
        (CCUINT32)sizeof(MKContinuation);
    for (CCUINT32 i = 0; i < cont_count; i++)
        mk_value_clear(&conts[i].operand);
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
    result->kind = MK_EXEC_NORMAL;
}

static MKASTNodeId rt_child(
    const MKRuntime *runtime, MKASTNodeId parent, CCUINT32 index
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, parent);
    if (!node)
        return MK_AST_NODE_NIL;
    MKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < index && child != MK_AST_NODE_NIL; i++)
    {
        const MKASTNode *child_node =
            mk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : MK_AST_NODE_NIL;
    }
    return child;
}

void mk_runtime_mark_fatal(MKRuntime *runtime)
{
    if (runtime)
        runtime->failed = CCTRUE;
}

CCBOOL mk_runtime_failed(const MKRuntime *runtime)
{
    return (runtime && runtime->failed) ? CCTRUE : CCFALSE;
}

/* ============================================================================
 * 环境
 * ========================================================================== */

static MKEnv *env_create(MKEnv *parent)
{
    MKEnv *env = ccalloc(NULL, sizeof(MKEnv), MK_RT_COMMENT);
    if (!env)
        return NULL;
    memset(env, 0, sizeof(*env));
    env->parent = parent;
    return env;
}

static void env_destroy(MKEnv *env)
{
    if (!env)
        return;
    MKEnvVar *vars = (MKEnvVar *)env->vars.array;
    CCUINT32 count = env->vars.size / (CCUINT32)sizeof(MKEnvVar);
    for (CCUINT32 i = 0; i < count; i++)
    {
        mk_string_clear(&vars[i].name);
        mk_value_clear(&vars[i].value);
    }
    if (env->vars.array)
        ccalloc(env->vars.array, 0, NULL);
    ccalloc(env, 0, NULL);
}

static MKEnvVar *env_slot(
    MKEnv *env, const char *name, CCUINT32 len
)
{
    for (MKEnv *current = env; current; current = current->parent)
    {
        MKEnvVar *vars = (MKEnvVar *)current->vars.array;
        CCUINT32 count =
            current->vars.size / (CCUINT32)sizeof(MKEnvVar);
        for (CCUINT32 i = 0; i < count; i++)
        {
            MKEnvVar *var = &vars[i];
            if (var->name.len == len &&
                (len == 0 || memcmp(var->name.data, name, len) == 0))
                return var;
        }
    }
    return NULL;
}

static CCBOOL env_define(
    MKRuntime *runtime, MKEnv *env,
    const char *name, CCUINT32 len,
    const MKValue *value, CCBOOL is_const, MKASTNodeId node
)
{
    if (!env || !name || !value)
        return rt_error(runtime, node, "内部错误：环境定义参数无效");

    MKEnvVar *vars = (MKEnvVar *)env->vars.array;
    CCUINT32 count = env->vars.size / (CCUINT32)sizeof(MKEnvVar);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (vars[i].name.len == len &&
            (len == 0 || memcmp(vars[i].name.data, name, len) == 0))
            return rt_error(runtime, node, "重复的变量：%.*s", (int)len, name);
    }

    MKEnvVar var;
    memset(&var, 0, sizeof(var));
    if (!mk_string_set(&var.name, name, len))
        return rt_error(runtime, node, "内存分配失败");
    if (!mk_value_copy(&var.value, value))
    {
        mk_string_clear(&var.name);
        return rt_error(runtime, node, "值复制失败");
    }
    var.is_const = is_const;
    if (!mk_dyn_push(
            &env->vars, (CCUINT32)sizeof(MKEnvVar),
            (CCINT32)sizeof(MKEnvVar), &var
        ))
    {
        mk_string_clear(&var.name);
        mk_value_clear(&var.value);
        return rt_error(runtime, node, "内存分配失败");
    }
    return CCTRUE;
}

/* ============================================================================
 * 值类型与算术
 * ========================================================================== */

static MKType value_type(const MKValue *value)
{
    if (!value)
        return mk_type_scalar(MK_VALUE_VOID);
    if (value->type == MK_VALUE_LIST)
    {
        MKValueType element = value->as.list
            ? value->as.list->element_type : MK_VALUE_VOID;
        return mk_type_list(element);
    }
    return mk_type_scalar(value->type);
}

static CCBOOL rt_expect_value(
    MKRuntime *runtime, MKASTNodeId node,
    const MKValue *value, const MKType *expected
)
{
    MKType actual = value_type(value);
    if (mk_type_equal(&actual, expected))
        return CCTRUE;
    char actual_buf[64];
    char expected_buf[64];
    mk_type_format(&actual, actual_buf, sizeof(actual_buf));
    mk_type_format(expected, expected_buf, sizeof(expected_buf));
    return rt_error(
        runtime, node, "运行时类型不匹配：需要 %s，实际 %s",
        expected_buf, actual_buf
    );
}

static CCBOOL int_add(CCINT64 a, CCINT64 b, CCINT64 *out)
{
    if ((b > 0 && a > MK_INT64_MAX_ - b) ||
        (b < 0 && a < MK_INT64_MIN_ - b))
        return CCFALSE;
    *out = a + b;
    return CCTRUE;
}

static CCBOOL int_sub(CCINT64 a, CCINT64 b, CCINT64 *out)
{
    if ((b > 0 && a < MK_INT64_MIN_ + b) ||
        (b < 0 && a > MK_INT64_MAX_ + b))
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
    if (a == MK_INT64_MIN_ && b == -1)
        return CCFALSE;
    if (b == MK_INT64_MIN_ && a == -1)
        return CCFALSE;

    if ((a > 0 && b > 0 && a > MK_INT64_MAX_ / b) ||
        (a > 0 && b < 0 && b < MK_INT64_MIN_ / a) ||
        (a < 0 && b > 0 && a < MK_INT64_MIN_ / b) ||
        (a < 0 && b < 0 && a < MK_INT64_MAX_ / b))
        return CCFALSE;
    *out = a * b;
    return CCTRUE;
}

static int string_compare(const MKString *a, const MKString *b)
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
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env, MKValue *out
);

static CCBOOL rt_eval_config(
    MKRuntime *runtime, MKASTNodeId id, MKValue *out
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != MK_AST_CONFIG_REF)
        return rt_error(runtime, id, "内部错误：配置引用节点");

    CCUINT32 path_len = 0;
    const char *path = mk_ast_string(
        runtime->semantic->ast, node->as.config_ref.path, &path_len
    );
    if (!path)
        return rt_error(runtime, id, "配置路径无效");

    MKValueType expected_element = MK_VALUE_VOID;
    const MKType *type = mk_semantic_node_type(runtime->semantic, id);
    if (type && type->kind == MK_VALUE_LIST)
        expected_element = type->element;

    MKConfigResult result = mk_config_value(
        runtime->config, path, expected_element, out
    );
    switch (result)
    {
    case MK_CONFIG_OK:
        break;

    case MK_CONFIG_MISSING:
        return rt_error(runtime, id, "配置字段不存在：%s", path);

    case MK_CONFIG_MIXED:
        return rt_error(runtime, id, "配置数组元素类型不一致：%s", path);

    case MK_CONFIG_NOMEM:
        return rt_error(runtime, id, "配置查询内存分配失败");

    default:
        return rt_error(runtime, id, "配置字段类型不受支持：%s", path);
    }

    if (type && !rt_expect_value(runtime, id, out, type))
    {
        mk_value_clear(out);
        return CCFALSE;
    }
    return CCTRUE;
}


static CCBOOL rt_eval_ident(
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env, MKValue *out
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != MK_AST_IDENT)
        return rt_error(runtime, id, "内部错误：标识符节点");

    CCUINT32 name_len = 0;
    const char *name = mk_ast_string(
        runtime->semantic->ast, node->as.ident.name, &name_len
    );
    if (!name)
        return rt_error(runtime, id, "变量名无效");

    MKEnvVar *slot = env_slot(env, name, name_len);
    if (!slot)
        return rt_error(runtime, id, "未定义的变量：%.*s", (int)name_len, name);
    if (!mk_value_copy(out, &slot->value))
        return rt_error(runtime, id, "值复制失败");
    return CCTRUE;
}




static CCBOOL rt_apply_unary(
    MKRuntime *runtime, MKASTNodeId id, MKUnaryOp op,
    const MKValue *value, MKValue *out
)
{
    switch (op)
    {
    case MK_UNARY_NOT:
        if (value->type != MK_VALUE_BOOL)
            return rt_error(runtime, id, "! 需要 bool");
        return mk_value_set_bool(
            out, value->as.boolean ? CCFALSE : CCTRUE
        );

    case MK_UNARY_NEG:
        if (value->type == MK_VALUE_INT)
        {
            if (value->as.integer == MK_INT64_MIN_)
                return rt_error(runtime, id, "整数取负溢出");
            return mk_value_set_int(out, -value->as.integer);
        }
        if (value->type == MK_VALUE_FLOAT)
            return mk_value_set_float(out, -value->as.floating);
        return rt_error(runtime, id, "- 需要数值");

    case MK_UNARY_POS:
        if (value->type == MK_VALUE_INT ||
            value->type == MK_VALUE_FLOAT)
            return mk_value_copy(out, value);
        return rt_error(runtime, id, "+ 需要数值");

    default:
        return rt_error(runtime, id, "未知一元运算符");
    }
}

static CCBOOL rt_apply_binary(
    MKRuntime *runtime, MKASTNodeId id, MKBinaryOp op,
    const MKValue *left, const MKValue *right, MKValue *out
)
{
    switch (op)
    {
    case MK_BINARY_EQ:
    case MK_BINARY_NE:
        if (left->type != right->type)
            return rt_error(runtime, id, "比较两侧类型不一致");
        {
            CCBOOL equal = mk_value_equal(left, right);
            if (op == MK_BINARY_NE)
                equal = equal ? CCFALSE : CCTRUE;
            return mk_value_set_bool(out, equal);
        }

    case MK_BINARY_LT:
    case MK_BINARY_LE:
    case MK_BINARY_GT:
    case MK_BINARY_GE:
    {
        int cmp = 0;
        if (left->type == MK_VALUE_INT && right->type == MK_VALUE_INT)
            cmp = left->as.integer < right->as.integer ? -1
                : (left->as.integer > right->as.integer ? 1 : 0);
        else if (left->type == MK_VALUE_FLOAT &&
                 right->type == MK_VALUE_FLOAT)
            cmp = left->as.floating < right->as.floating ? -1
                : (left->as.floating > right->as.floating ? 1 : 0);
        else if (left->type == MK_VALUE_STRING &&
                 right->type == MK_VALUE_STRING)
            cmp = string_compare(&left->as.string, &right->as.string);
        else
            return rt_error(runtime, id, "比较类型不受支持");
        CCBOOL result = CCFALSE;
        switch (op)
        {
        case MK_BINARY_LT:
            result = cmp < 0;
            break;
        case MK_BINARY_LE:
            result = cmp <= 0;
            break;
        case MK_BINARY_GT:
            result = cmp > 0;
            break;
        default:
            result = cmp >= 0;
            break;
        }
        return mk_value_set_bool(out, result);
    }

    case MK_BINARY_ADD:
        if (left->type == MK_VALUE_INT && right->type == MK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_add(left->as.integer, right->as.integer, &value))
                return rt_error(runtime, id, "整数加法溢出");
            return mk_value_set_int(out, value);
        }
        if (left->type == MK_VALUE_FLOAT &&
            right->type == MK_VALUE_FLOAT)
            return mk_value_set_float(
                out, left->as.floating + right->as.floating
            );
        if (left->type == MK_VALUE_STRING &&
            right->type == MK_VALUE_STRING)
        {
            MKString text;
            mk_string_init(&text);
            if (!mk_string_append(
                    &text, left->as.string.data, left->as.string.len
                ) ||
                !mk_string_append(
                    &text, right->as.string.data, right->as.string.len
                ))
            {
                mk_string_clear(&text);
                return rt_error(
                    runtime, id, "字符串拼接内存分配失败"
                );
            }
            CCBOOL ok = mk_value_set_string(out, text.data, text.len);
            mk_string_clear(&text);
            return ok;
        }
        if (left->type == MK_VALUE_LIST && right->type == MK_VALUE_LIST)
        {
            MKValueType element = left->as.list->element_type;
            if (element == MK_VALUE_VOID)
                element = right->as.list->element_type;
            if (element == MK_VALUE_VOID)
                return rt_error(runtime, id, "空列表类型未解析");
            if (!mk_value_set_list(out, element))
                return rt_error(runtime, id, "列表分配失败");
            CCUINT32 left_count = mk_list_count(left->as.list);
            for (CCUINT32 i = 0; i < left_count; i++)
            {
                if (!mk_list_append(
                        out->as.list, mk_list_at(left->as.list, i)
                    ))
                {
                    mk_value_clear(out);
                    return rt_error(runtime, id, "列表拼接失败");
                }
            }
            CCUINT32 right_count = mk_list_count(right->as.list);
            for (CCUINT32 i = 0; i < right_count; i++)
            {
                if (!mk_list_append(
                        out->as.list, mk_list_at(right->as.list, i)
                    ))
                {
                    mk_value_clear(out);
                    return rt_error(runtime, id, "列表拼接失败");
                }
            }
            return CCTRUE;
        }
        return rt_error(runtime, id, "加法类型不受支持");

    case MK_BINARY_SUB:
        if (left->type == MK_VALUE_INT && right->type == MK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_sub(left->as.integer, right->as.integer, &value))
                return rt_error(runtime, id, "整数减法溢出");
            return mk_value_set_int(out, value);
        }
        if (left->type == MK_VALUE_FLOAT &&
            right->type == MK_VALUE_FLOAT)
            return mk_value_set_float(
                out, left->as.floating - right->as.floating
            );
        return rt_error(runtime, id, "减法类型不受支持");

    case MK_BINARY_MUL:
        if (left->type == MK_VALUE_INT && right->type == MK_VALUE_INT)
        {
            CCINT64 value = 0;
            if (!int_mul(left->as.integer, right->as.integer, &value))
                return rt_error(runtime, id, "整数乘法溢出");
            return mk_value_set_int(out, value);
        }
        if (left->type == MK_VALUE_FLOAT &&
            right->type == MK_VALUE_FLOAT)
            return mk_value_set_float(
                out, left->as.floating * right->as.floating
            );
        return rt_error(runtime, id, "乘法类型不受支持");

    case MK_BINARY_DIV:
        if (left->type == MK_VALUE_INT && right->type == MK_VALUE_INT)
        {
            if (right->as.integer == 0)
                return rt_error(runtime, id, "整数除数为零");
            if (left->as.integer == MK_INT64_MIN_ &&
                right->as.integer == -1)
                return rt_error(runtime, id, "整数除法溢出");
            return mk_value_set_int(
                out, left->as.integer / right->as.integer
            );
        }
        if (left->type == MK_VALUE_FLOAT &&
            right->type == MK_VALUE_FLOAT)
        {
            if (right->as.floating == 0.0)
                return rt_error(runtime, id, "浮点除数为零");
            return mk_value_set_float(
                out, left->as.floating / right->as.floating
            );
        }
        return rt_error(runtime, id, "除法类型不受支持");

    case MK_BINARY_MOD:
        if (left->type != MK_VALUE_INT || right->type != MK_VALUE_INT)
            return rt_error(runtime, id, "取模需要 int");
        if (right->as.integer == 0)
            return rt_error(runtime, id, "取模除数为零");
        if (left->as.integer == MK_INT64_MIN_ &&
            right->as.integer == -1)
            return rt_error(runtime, id, "取模溢出");
        return mk_value_set_int(
            out, left->as.integer % right->as.integer
        );

    default:
        return rt_error(runtime, id, "未知二元运算符");
    }
}

typedef enum mk_rt_expr_kind {
    MK_RT_EXPR_LITERAL = 0,
    MK_RT_EXPR_IDENT,
    MK_RT_EXPR_CONFIG,
    MK_RT_EXPR_MEMBER,
    MK_RT_EXPR_LIST,
    MK_RT_EXPR_UNARY,
    MK_RT_EXPR_BINARY,
    MK_RT_EXPR_INDEX,
    MK_RT_EXPR_CALL
} MKRtExprKind;

typedef enum mk_rt_expr_stage {
    MK_RT_EXPR_START = 0,
    MK_RT_EXPR_LIST_ITEM,
    MK_RT_EXPR_UNARY_OPERAND,
    MK_RT_EXPR_BINARY_LEFT,
    MK_RT_EXPR_BINARY_RIGHT,
    MK_RT_EXPR_INDEX_BASE,
    MK_RT_EXPR_INDEX_INDEX
} MKRtExprStage;

typedef struct mk_rt_expr_frame {
    MKRtExprKind kind;
    MKRtExprStage stage;
    MKASTNodeId id;
    MKEnv *env;
    MKValue value;
    MKBinaryOp op;
    MKUnaryOp unary_op;
    CCUINT32 index;
    CCUINT32 count;
    MKValue *args;
    CCUINT32 argc;
    const MKFunctionInfo *function;
    const MKBuiltinSpec *builtin;
} MKRtExprFrame;

static MKRtExprFrame *rt_expr_top(CCDyn *frames)
{
    if (!frames || frames->size < (CCUINT32)sizeof(MKRtExprFrame))
        return NULL;
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKRtExprFrame);
    return &((MKRtExprFrame *)frames->array)[count - 1];
}

static CCBOOL rt_expr_push(
    MKRuntime *runtime, CCDyn *frames,
    MKASTNodeId id, MKEnv *env
)
{
    MKRtExprFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = MK_RT_EXPR_LITERAL;
    frame.stage = MK_RT_EXPR_START;
    frame.id = id;
    frame.env = env;
    mk_value_init(&frame.value);
    if (!mk_dyn_push(
            frames, (CCUINT32)sizeof(MKRtExprFrame),
            (CCINT32)sizeof(MKRtExprFrame), &frame
        ))
        return rt_error(runtime, id, "内存分配失败");
    return CCTRUE;
}

static CCBOOL rt_expr_pop(CCDyn *frames, MKRtExprFrame *out)
{
    return (cc_dyn_core_pop(
        frames, (CCUINT32)sizeof(MKRtExprFrame),
        (CCINT32)sizeof(MKRtExprFrame), out
    ) == 0) ? CCTRUE : CCFALSE;
}

static void rt_expr_frame_cleanup(MKRtExprFrame *frame)
{
    mk_value_clear(&frame->value);
    if (frame->args)
    {
        for (CCUINT32 i = 0; i < frame->argc; i++)
            mk_value_clear(&frame->args[i]);
        ccalloc(frame->args, 0, NULL);
        frame->args = NULL;
        frame->argc = 0;
    }
}

static void rt_expr_frames_abort(CCDyn *frames)
{
    CCUINT32 count = frames->size / (CCUINT32)sizeof(MKRtExprFrame);
    for (CCUINT32 i = 0; i < count; i++)
        rt_expr_frame_cleanup(&((MKRtExprFrame *)frames->array)[i]);
    if (frames->array)
        ccalloc(frames->array, 0, NULL);
    memset(frames, 0, sizeof(*frames));
}

static CCBOOL rt_expr_list_next(
    MKRuntime *runtime, MKRtExprFrame *frame, CCDyn *frames
)
{
    if (frame->index >= frame->count)
        return CCTRUE;
    MKASTNodeId item = rt_child(runtime, frame->id, frame->index);
    if (item == MK_AST_NODE_NIL)
        return rt_error(runtime, frame->id, "列表元素缺失");
    return rt_expr_push(runtime, frames, item, frame->env);
}

static CCBOOL rt_invoke_call(
    MKRuntime *runtime, MKASTNodeId id,
    const MKFunctionInfo *function, const MKBuiltinSpec *spec,
    MKValue *args, CCUINT32 argc, MKValue *out
)
{
    if (function)
        return mk_runtime_call_function(
            runtime, function, args, argc, out
        );
    if (!spec)
        return rt_error(runtime, id, "调用未完成语义绑定");

    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "调用节点缺失");

    MKValue result;
    mk_value_init(&result);
    MKBuiltinCall call;
    memset(&call, 0, sizeof(call));
    call.runtime = runtime;
    call.args = argc > 0 ? args : NULL;
    call.argc = argc;
    call.out = &result;
    call.diagnostic = runtime->diagnostic;
    call.file = mk_ast_file(runtime->semantic->ast);
    call.line = node->span.line;
    call.col = node->span.col;

    CCBOOL ok = spec->fn(&call);
    if (!ok)
    {
        mk_value_clear(&result);
        if (runtime->failed)
            return CCFALSE;
        return rt_error(runtime, id, "内建调用失败：%s", spec->name);
    }

    MKType actual = value_type(&result);
    if (!mk_type_equal(&actual, &spec->return_type))
    {
        mk_value_clear(&result);
        return rt_error(runtime, id, "内建返回值类型错误：%s", spec->name);
    }

    ok = mk_value_copy(out, &result);
    mk_value_clear(&result);
    return ok ? CCTRUE : rt_error(runtime, id, "返回值复制失败");
}

static CCBOOL rt_expr_call_next(
    MKRuntime *runtime, MKRtExprFrame *frame, CCDyn *frames
)
{
    if (frame->index >= frame->argc)
        return CCTRUE;
    MKASTNodeId arg = rt_child(runtime, frame->id, frame->index);
    if (arg == MK_AST_NODE_NIL)
        return rt_error(runtime, frame->id, "调用实参缺失");
    return rt_expr_push(runtime, frames, arg, frame->env);
}

static CCBOOL rt_expr_literal(
    MKRuntime *runtime, MKASTNodeId id,
    const MKASTNode *node, MKValue *out, CCBOOL *done
)
{
    switch (node->as.literal.kind)
    {
    case MK_LITERAL_INT:
        *done = CCTRUE;
        return mk_value_set_int(out, node->as.literal.value.integer);
    case MK_LITERAL_FLOAT:
        *done = CCTRUE;
        return mk_value_set_float(out, node->as.literal.value.floating);
    case MK_LITERAL_STRING:
    {
        CCUINT32 len = 0;
        const char *text = mk_ast_string(
            runtime->semantic->ast,
            node->as.literal.value.string, &len
        );
        *done = CCTRUE;
        return text
            ? mk_value_set_string(out, text, len)
            : rt_error(runtime, id, "字符串值无效");
    }
    case MK_LITERAL_BOOL:
        *done = CCTRUE;
        return mk_value_set_bool(out, node->as.literal.value.boolean);
    default:
        return rt_error(runtime, id, "未知字面量");
    }
}

static CCBOOL rt_expr_step_start(
    MKRuntime *runtime, MKRtExprFrame *frame, CCDyn *frames,
    MKValue *input, MKValue *out, CCBOOL *done
)
{
    (void)frames;
    (void)input;
    if (!rt_step(runtime, frame->id))
        return CCFALSE;
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, frame->id);
    if (!node)
        return rt_error(runtime, frame->id, "表达式节点缺失");

    switch (node->kind)
    {
    case MK_AST_LITERAL:
        return rt_expr_literal(runtime, frame->id, node, out, done);

    case MK_AST_IDENT:
        frame->kind = MK_RT_EXPR_IDENT;
        *done = CCTRUE;
        return rt_eval_ident(runtime, frame->id, frame->env, out);

    case MK_AST_CONFIG_REF:
        frame->kind = MK_RT_EXPR_CONFIG;
        *done = CCTRUE;
        return rt_eval_config(runtime, frame->id, out);

    case MK_AST_LIST:
    {
        const MKType *type = mk_semantic_node_type(
            runtime->semantic, frame->id
        );
        if (!type || type->kind != MK_VALUE_LIST ||
            type->element == MK_VALUE_VOID)
            return rt_error(
                runtime, frame->id, "空列表或列表元素类型未解析"
            );
        frame->kind = MK_RT_EXPR_LIST;
        frame->index = 0;
        frame->count = 0;
        for (MKASTNodeId child = node->first_child;
             child != MK_AST_NODE_NIL;)
        {
            frame->count++;
            const MKASTNode *child_node =
                mk_ast_node(runtime->semantic->ast, child);
            child = child_node
                ? child_node->next_sibling : MK_AST_NODE_NIL;
        }
        if (!mk_value_set_list(&frame->value, type->element))
            return rt_error(runtime, frame->id, "列表分配失败");
        if (frame->count > 0)
        {
            if (!rt_expr_list_next(runtime, frame, frames))
                return CCFALSE;
            *done = CCFALSE;
            return CCTRUE;
        }
        *out = frame->value;
        mk_value_init(&frame->value);
        *done = CCTRUE;
        return CCTRUE;
    }

    case MK_AST_UNARY:
        frame->kind = MK_RT_EXPR_UNARY;
        frame->unary_op = node->as.unary.op;
        frame->stage = MK_RT_EXPR_UNARY_OPERAND;
        if (!rt_expr_push(
                runtime, frames, node->first_child, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;

    case MK_AST_BINARY:
        frame->kind = MK_RT_EXPR_BINARY;
        frame->op = node->as.binary.op;
        frame->stage = MK_RT_EXPR_BINARY_LEFT;
        if (!rt_expr_push(
                runtime, frames, node->first_child, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;

    case MK_AST_INDEX:
        frame->kind = MK_RT_EXPR_INDEX;
        frame->stage = MK_RT_EXPR_INDEX_BASE;
        if (!rt_expr_push(
                runtime, frames, node->first_child, frame->env
            ))
            return CCFALSE;
        *done = CCFALSE;
        return CCTRUE;

    case MK_AST_CALL:
    {
        frame->kind = MK_RT_EXPR_CALL;
        frame->index = 0;
        frame->argc = 0;
        for (MKASTNodeId child = node->first_child;
             child != MK_AST_NODE_NIL;)
        {
            frame->argc++;
            const MKASTNode *child_node =
                mk_ast_node(runtime->semantic->ast, child);
            child = child_node
                ? child_node->next_sibling : MK_AST_NODE_NIL;
        }
        frame->function = mk_semantic_node_function(
            runtime->semantic, frame->id
        );
        frame->builtin = mk_semantic_node_builtin(
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
                (CCUINT64)frame->argc * sizeof(MKValue),
                MK_RT_COMMENT
            );
            if (!frame->args)
                return rt_error(runtime, frame->id, "内存分配失败");
            for (CCUINT32 i = 0; i < frame->argc; i++)
                mk_value_init(&frame->args[i]);
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
            mk_ast_kind_name(node->kind)
        );
    }
}

static CCBOOL rt_expr_step_resume(
    MKRuntime *runtime, MKRtExprFrame *frame, CCDyn *frames,
    MKValue *input, MKValue *out, CCBOOL *done
)
{
    switch (frame->kind)
    {
case MK_RT_EXPR_LIST:
    if (!mk_list_append(frame->value.as.list, input))
        return rt_error(runtime, frame->id, "列表追加失败");
    mk_value_clear(input);
    frame->index++;
    if (frame->index >= frame->count)
    {
        *out = frame->value;
        mk_value_init(&frame->value);
        *done = CCTRUE;
        return CCTRUE;
    }
    if (!rt_expr_list_next(runtime, frame, frames))
        return CCFALSE;
    *done = CCFALSE;
    return CCTRUE;

case MK_RT_EXPR_UNARY:
    if (!rt_apply_unary(
            runtime, frame->id, frame->unary_op, input, out
        ))
        return CCFALSE;
    mk_value_clear(input);
    *done = CCTRUE;
    return CCTRUE;

case MK_RT_EXPR_BINARY:
    if (frame->stage == MK_RT_EXPR_BINARY_LEFT)
    {
        if (frame->op == MK_BINARY_AND ||
            frame->op == MK_BINARY_OR)
        {
            if (input->type != MK_VALUE_BOOL)
            {
                mk_value_clear(input);
                return rt_error(
                    runtime, frame->id, "逻辑运算需要 bool"
                );
            }
            if (frame->op == MK_BINARY_AND && !input->as.boolean)
            {
                mk_value_clear(input);
                *done = CCTRUE;
                return mk_value_set_bool(out, CCFALSE);
            }
            if (frame->op == MK_BINARY_OR && input->as.boolean)
            {
                mk_value_clear(input);
                *done = CCTRUE;
                return mk_value_set_bool(out, CCTRUE);
            }
        }
        frame->value = *input;
        mk_value_init(input);
        frame->stage = MK_RT_EXPR_BINARY_RIGHT;
        const MKASTNode *node =
            mk_ast_node(runtime->semantic->ast, frame->id);
        MKASTNodeId right_id = node
            ? node->first_child : MK_AST_NODE_NIL;
        const MKASTNode *right_node = mk_ast_node(
            runtime->semantic->ast, right_id
        );
        right_id = right_node
            ? right_node->next_sibling : MK_AST_NODE_NIL;
        if (right_id == MK_AST_NODE_NIL)
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

    if (frame->op == MK_BINARY_AND ||
        frame->op == MK_BINARY_OR)
    {
        if (input->type != MK_VALUE_BOOL)
        {
            mk_value_clear(input);
            mk_value_clear(&frame->value);
            return rt_error(
                runtime, frame->id, "逻辑运算需要 bool"
            );
        }
        CCBOOL result = input->as.boolean;
        mk_value_clear(input);
        mk_value_clear(&frame->value);
        *done = CCTRUE;
        return mk_value_set_bool(out, result);
    }

    if (!rt_apply_binary(
            runtime, frame->id, frame->op,
            &frame->value, input, out
        ))
    {
        mk_value_clear(&frame->value);
        mk_value_clear(input);
        return CCFALSE;
    }
    mk_value_clear(&frame->value);
    mk_value_clear(input);
    *done = CCTRUE;
    return CCTRUE;

case MK_RT_EXPR_INDEX:
    if (frame->stage == MK_RT_EXPR_INDEX_BASE)
    {
        frame->value = *input;
        mk_value_init(input);
        frame->stage = MK_RT_EXPR_INDEX_INDEX;
        const MKASTNode *node =
            mk_ast_node(runtime->semantic->ast, frame->id);
        MKASTNodeId index_id = node
            ? node->first_child : MK_AST_NODE_NIL;
        const MKASTNode *index_node = mk_ast_node(
            runtime->semantic->ast, index_id
        );
        index_id = index_node
            ? index_node->next_sibling : MK_AST_NODE_NIL;
        if (index_id == MK_AST_NODE_NIL)
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

    if (frame->value.type != MK_VALUE_LIST ||
        input->type != MK_VALUE_INT)
    {
        mk_value_clear(&frame->value);
        mk_value_clear(input);
        return rt_error(runtime, frame->id, "索引需要 list[int]");
    }
    {
        CCINT64 at = input->as.integer;
        if (at < 0 ||
            (CCUINT64)at >= mk_list_count(frame->value.as.list))
        {
            mk_value_clear(&frame->value);
            mk_value_clear(input);
            return rt_error(runtime, frame->id, "列表索引越界");
        }
        CCBOOL ok = mk_value_copy(
            out, mk_list_at(frame->value.as.list, (CCUINT32)at)
        );
        mk_value_clear(&frame->value);
        mk_value_clear(input);
        *done = CCTRUE;
        return ok ? CCTRUE : rt_error(
            runtime, frame->id, "索引值复制失败"
        );
    }

case MK_RT_EXPR_CALL:
    frame->args[frame->index] = *input;
    mk_value_init(input);
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
        mk_value_clear(&frame->args[i]);
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
    MKRuntime *runtime, MKRtExprFrame *frame, CCDyn *frames,
    CCBOOL has_input, MKValue *input, MKValue *out, CCBOOL *done
)
{
    if (!has_input)
        return rt_expr_step_start(runtime, frame, frames, input, out, done);
    return rt_expr_step_resume(runtime, frame, frames, input, out, done);
}

static CCBOOL rt_eval_expr_run(
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env, MKValue *out
)
{
    CCDyn frames;
    memset(&frames, 0, sizeof(frames));
    CCBOOL has_completed = CCFALSE;
    MKValue completed;
    mk_value_init(&completed);

    for (;;)
    {
        if (has_completed)
        {
            MKRtExprFrame *frame = rt_expr_top(&frames);
            if (!frame)
            {
                *out = completed;
                mk_value_init(&completed);
                ccalloc(frames.array, 0, NULL);
                return CCTRUE;
            }
            MKValue result;
            mk_value_init(&result);
            CCBOOL done = CCFALSE;
            if (!rt_expr_step(
                    runtime, frame, &frames, CCTRUE, &completed,
                    &result, &done
                ))
                goto Failed;
            if (done)
            {
                rt_expr_frame_cleanup(frame);
                MKRtExprFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (!rt_expr_pop(&frames, &popped))
                    goto Failed;
                completed = result;
                mk_value_init(&result);
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

        MKRtExprFrame *frame = rt_expr_top(&frames);
        if (!frame)
            goto Failed;
        MKValue result;
        mk_value_init(&result);
        CCBOOL done = CCFALSE;
        if (!rt_expr_step(
                runtime, frame, &frames, CCFALSE, NULL,
                &result, &done
            ))
            goto Failed;
        if (done)
        {
            rt_expr_frame_cleanup(frame);
            MKRtExprFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (!rt_expr_pop(&frames, &popped))
                goto Failed;
            completed = result;
            mk_value_init(&result);
            has_completed = CCTRUE;
        }
    }

Failed:
    mk_value_clear(&completed);
    rt_expr_frames_abort(&frames);
    return CCFALSE;
}


static CCBOOL rt_eval_expr(
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env, MKValue *out
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
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env,
    MKValue *value
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != MK_AST_VAR_DECL)
    {
        mk_value_clear(value);
        return rt_error(runtime, id, "内部错误：变量声明");
    }

    CCUINT32 name_len = 0;
    const char *name = mk_ast_string(
        runtime->semantic->ast, node->as.var_decl.name, &name_len
    );
    CCBOOL ok = name && env_define(
        runtime, env, name, name_len, value,
        node->as.var_decl.is_const, id
    );
    if (!ok && !name)
        ok = rt_error(runtime, id, "变量名无效");
    if (!ok && name && !mk_diagnostic_has(runtime->diagnostic))
        ok = rt_error(runtime, id, "变量 %s 初始化失败", name);
    mk_value_clear(value);
    return ok;
}

static CCBOOL rt_eval_var_decl(
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != MK_AST_VAR_DECL)
        return rt_error(runtime, id, "内部错误：变量声明");

    MKASTNodeId init_id = MK_AST_NODE_NIL;
    MKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < 1 && child != MK_AST_NODE_NIL; i++)
    {
        const MKASTNode *child_node =
            mk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : MK_AST_NODE_NIL;
    }
    init_id = child;

    MKValue value;
    mk_value_init(&value);
    if (!rt_eval_expr(runtime, init_id, env, &value))
    {
        mk_value_clear(&value);
        return CCFALSE;
    }

    return rt_var_decl_apply(runtime, id, env, &value);
}

static CCBOOL rt_assign_apply(
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env,
    MKValue *value
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != MK_AST_ASSIGN)
    {
        mk_value_clear(value);
        return rt_error(runtime, id, "内部错误：赋值语句");
    }
    MKASTNodeId target_id = node->first_child;
    const MKASTNode *target =
        mk_ast_node(runtime->semantic->ast, target_id);
    if (!target)
    {
        mk_value_clear(value);
        return rt_error(runtime, id, "赋值目标缺失");
    }
    if (target->kind == MK_AST_DISCARD)
    {
        mk_value_clear(value);
        return CCTRUE;
    }
    if (target->kind != MK_AST_IDENT)
    {
        mk_value_clear(value);
        return rt_error(runtime, id, "赋值目标非法");
    }

    CCUINT32 name_len = 0;
    const char *name = mk_ast_string(
        runtime->semantic->ast, target->as.ident.name, &name_len
    );
    if (!name)
    {
        mk_value_clear(value);
        return rt_error(runtime, id, "变量名无效");
    }
    MKEnvVar *slot = env_slot(env, name, name_len);
    if (!slot)
    {
        mk_value_clear(value);
        return rt_error(runtime, id, "未定义的变量：%.*s", (int)name_len, name);
    }
    if (slot->is_const)
    {
        mk_value_clear(value);
        return rt_error(
            runtime, id, "const 变量不能重新赋值：%.*s",
            (int)name_len, name
        );
    }

    CCBOOL ok = CCFALSE;
    if (node->as.assign.op == MK_ASSIGN_SET)
    {
        ok = mk_value_copy(&slot->value, value);
        if (!ok)
            ok = rt_error(runtime, id, "值复制失败");
    }
    else if (slot->value.type == MK_VALUE_LIST)
    {
        if (value->type != slot->value.as.list->element_type)
            ok = rt_error(runtime, id, "list += 元素类型不匹配");
        else
            ok = mk_list_append(slot->value.as.list, value)
                ? CCTRUE : rt_error(runtime, id, "list += 追加失败");
    }
    else if (slot->value.type == MK_VALUE_INT && value->type == MK_VALUE_INT)
    {
        CCINT64 sum = 0;
        if (!int_add(slot->value.as.integer, value->as.integer, &sum))
            ok = rt_error(runtime, id, "整数 += 溢出");
        else
            ok = mk_value_set_int(&slot->value, sum);
    }
    else if (slot->value.type == MK_VALUE_FLOAT &&
             value->type == MK_VALUE_FLOAT)
        ok = mk_value_set_float(
            &slot->value, slot->value.as.floating + value->as.floating
        );
    else
        ok = rt_error(runtime, id, "+= 类型不受支持");

    mk_value_clear(value);
    return ok;
}

static CCBOOL rt_eval_assign(
    MKRuntime *runtime, MKASTNodeId id, MKEnv *env
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node || node->kind != MK_AST_ASSIGN)
        return rt_error(runtime, id, "内部错误：赋值语句");

    MKASTNodeId target_id = node->first_child;
    const MKASTNode *target =
        mk_ast_node(runtime->semantic->ast, target_id);
    if (!target)
        return rt_error(runtime, id, "赋值目标缺失");
    MKASTNodeId value_id = target->next_sibling;

    MKValue value;
    mk_value_init(&value);
    if (!rt_eval_expr(runtime, value_id, env, &value))
    {
        mk_value_clear(&value);
        return CCFALSE;
    }

    return rt_assign_apply(runtime, id, env, &value);
}

typedef enum mk_rt_frame_kind {
    MK_RT_FRAME_BLOCK = 0,
    MK_RT_FRAME_IF,
    MK_RT_FRAME_WHILE,
    MK_RT_FRAME_FOR
} MKRtFrameKind;

typedef enum mk_rt_frame_stage {
    MK_RT_STAGE_ITEMS = 0,
    MK_RT_STAGE_BODY,
    MK_RT_STAGE_DONE
} MKRtFrameStage;

typedef struct mk_rt_frame {
    MKRtFrameKind kind;
    MKRtFrameStage stage;
    MKASTNodeId id;
    MKEnv *env;
    CCBOOL owns_env;
    MKASTNodeId next_child;
    MKASTNodeId body;
    MKValue iterable;
    const char *name;
    CCUINT32 name_len;
    CCUINT32 for_index;
    CCUINT32 for_count;
} MKRtFrame;

static CCBOOL rt_expr_scan_user_call(
    MKRuntime *runtime, MKASTNodeId root, CCBOOL *has_call
)
{
    *has_call = CCFALSE;
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    if (!mk_dyn_push(
            &stack, (CCUINT32)sizeof(MKASTNodeId),
            (CCINT32)sizeof(MKASTNodeId), &root
        ))
        return rt_error(runtime, root, "内存分配失败");

    while (stack.size >= (CCUINT32)sizeof(MKASTNodeId))
    {
        MKASTNodeId id = MK_AST_NODE_NIL;
        if (cc_dyn_core_pop(
                &stack, (CCUINT32)sizeof(MKASTNodeId),
                (CCINT32)sizeof(MKASTNodeId), &id
            ) != 0)
        {
            ccalloc(stack.array, 0, NULL);
            return rt_error(runtime, root, "调用扫描栈损坏");
        }
        const MKASTNode *node =
            mk_ast_node(runtime->semantic->ast, id);
        if (!node)
        {
            ccalloc(stack.array, 0, NULL);
            return rt_error(runtime, id, "表达式节点缺失");
        }
        if (node->kind == MK_AST_CALL &&
            mk_semantic_node_function(runtime->semantic, id))
        {
            *has_call = CCTRUE;
            ccalloc(stack.array, 0, NULL);
            return CCTRUE;
        }
        for (MKASTNodeId child = node->first_child;
             child != MK_AST_NODE_NIL;)
        {
            if (!mk_dyn_push(
                    &stack, (CCUINT32)sizeof(MKASTNodeId),
                    (CCINT32)sizeof(MKASTNodeId), &child
                ))
            {
                ccalloc(stack.array, 0, NULL);
                return rt_error(runtime, id, "内存分配失败");
            }
            const MKASTNode *child_node =
                mk_ast_node(runtime->semantic->ast, child);
            child = child_node
                ? child_node->next_sibling : MK_AST_NODE_NIL;
        }
    }

    ccalloc(stack.array, 0, NULL);
    return CCTRUE;
}



typedef enum mk_expr_insn_kind {
    MK_EXPR_INS_VALUE = 0,
    MK_EXPR_INS_UNARY,
    MK_EXPR_INS_BINARY,
    MK_EXPR_INS_INDEX,
    MK_EXPR_INS_CALL
} MKExprInsnKind;

typedef struct mk_expr_insn {
    MKExprInsnKind kind;
    MKASTNodeId id;
    MKUnaryOp unary;
    MKBinaryOp binary;
    const MKFunctionInfo *function;
    const MKBuiltinSpec *builtin;
    CCUINT32 argc;
} MKExprInsn;

struct mk_expr_program {
    CCDyn insns;
    CCDyn values;
    CCUINT32 ip;
    MKEnv *env;
    CCBOOL owns_env;
};

typedef struct mk_prog_frame {
    MKExprProgram program;
    const MKFunctionInfo *function;
} MKProgFrame;

static void rt_expr_program_clear(MKExprProgram *program);



static void rt_call_args_clear(MKValue *args, CCUINT32 argc);

static void rt_expr_program_clear(MKExprProgram *program)
{
    if (!program)
        return;
    MKValue *values = (MKValue *)program->values.array;
    CCUINT32 count = program->values.size / (CCUINT32)sizeof(MKValue);
    for (CCUINT32 i = 0; i < count; i++)
        mk_value_clear(&values[i]);
    if (program->values.array)
        ccalloc(program->values.array, 0, NULL);
    if (program->insns.array)
        ccalloc(program->insns.array, 0, NULL);
    if (program->owns_env && program->env)
        env_destroy(program->env);
    memset(program, 0, sizeof(*program));
}

static CCBOOL rt_compile_expr(
    MKRuntime *runtime, MKASTNodeId id, CCDyn *insns
)
{
    CCBOOL has_call = CCFALSE;
    if (!rt_expr_scan_user_call(runtime, id, &has_call))
        return CCFALSE;
    if (!has_call)
    {
        MKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = MK_EXPR_INS_VALUE;
        insn.id = id;
        return mk_dyn_push(
            insns, (CCUINT32)sizeof(MKExprInsn),
            (CCINT32)sizeof(MKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    const MKASTNode *node = mk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "表达式节点缺失");

    if (node->kind == MK_AST_CALL)
    {
        CCUINT32 argc = 0;
        for (MKASTNodeId arg = node->first_child;
             arg != MK_AST_NODE_NIL;)
        {
            argc++;
            if (!rt_compile_expr(runtime, arg, insns))
                return CCFALSE;
            const MKASTNode *arg_node =
                mk_ast_node(runtime->semantic->ast, arg);
            arg = arg_node
                ? arg_node->next_sibling : MK_AST_NODE_NIL;
        }
        MKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = MK_EXPR_INS_CALL;
        insn.id = id;
        insn.argc = argc;
        insn.function = mk_semantic_node_function(
            runtime->semantic, id
        );
        insn.builtin = mk_semantic_node_builtin(
            runtime->semantic, id
        );
        if (!insn.function && !insn.builtin)
            return rt_error(runtime, id, "调用未完成语义绑定");
        return mk_dyn_push(
            insns, (CCUINT32)sizeof(MKExprInsn),
            (CCINT32)sizeof(MKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    if (node->kind == MK_AST_UNARY)
    {
        MKASTNodeId operand = node->first_child;
        if (!rt_compile_expr(runtime, operand, insns))
            return CCFALSE;
        MKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = MK_EXPR_INS_UNARY;
        insn.id = id;
        insn.unary = node->as.unary.op;
        return mk_dyn_push(
            insns, (CCUINT32)sizeof(MKExprInsn),
            (CCINT32)sizeof(MKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    if (node->kind == MK_AST_BINARY)
    {
        MKASTNodeId left = node->first_child;
        const MKASTNode *left_node =
            mk_ast_node(runtime->semantic->ast, left);
        MKASTNodeId right = left_node
            ? left_node->next_sibling : MK_AST_NODE_NIL;
        if (!rt_compile_expr(runtime, left, insns) ||
            !rt_compile_expr(runtime, right, insns))
            return CCFALSE;
        MKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = MK_EXPR_INS_BINARY;
        insn.id = id;
        insn.binary = node->as.binary.op;
        return mk_dyn_push(
            insns, (CCUINT32)sizeof(MKExprInsn),
            (CCINT32)sizeof(MKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    if (node->kind == MK_AST_INDEX)
    {
        MKASTNodeId base = node->first_child;
        const MKASTNode *base_node =
            mk_ast_node(runtime->semantic->ast, base);
        MKASTNodeId index = base_node
            ? base_node->next_sibling : MK_AST_NODE_NIL;
        if (!rt_compile_expr(runtime, base, insns) ||
            !rt_compile_expr(runtime, index, insns))
            return CCFALSE;
        MKExprInsn insn;
        memset(&insn, 0, sizeof(insn));
        insn.kind = MK_EXPR_INS_INDEX;
        insn.id = id;
        return mk_dyn_push(
            insns, (CCUINT32)sizeof(MKExprInsn),
            (CCINT32)sizeof(MKExprInsn), &insn
        ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
    }

    MKExprInsn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = MK_EXPR_INS_VALUE;
    insn.id = id;
    return mk_dyn_push(
        insns, (CCUINT32)sizeof(MKExprInsn),
        (CCINT32)sizeof(MKExprInsn), &insn
    ) ? CCTRUE : rt_error(runtime, id, "内存分配失败");
}

static CCBOOL rt_program_push(
    MKRuntime *runtime, MKExprProgram *program, MKValue *value
)
{
    if (!mk_dyn_push(
            &program->values, (CCUINT32)sizeof(MKValue),
            (CCINT32)sizeof(MKValue), value
        ))
        return rt_error(runtime, MK_AST_NODE_NIL, "内存分配失败");
    mk_value_init(value);
    return CCTRUE;
}

static CCBOOL rt_program_pop(
    MKExprProgram *program, MKValue *value
)
{
    return (cc_dyn_core_pop(
        &program->values, (CCUINT32)sizeof(MKValue),
        (CCINT32)sizeof(MKValue), value
    ) == 0) ? CCTRUE : CCFALSE;
}

static CCBOOL rt_expr_program_value(
    MKRuntime *runtime, MKExprProgram *program,
    const MKExprInsn *insn
)
{
    MKValue value;
    mk_value_init(&value);
    if (!rt_eval_expr(runtime, insn->id, program->env, &value))
    {
        mk_value_clear(&value);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &value))
    {
        mk_value_clear(&value);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_unary(
    MKRuntime *runtime, MKExprProgram *program,
    const MKExprInsn *insn
)
{
    MKValue value;
    mk_value_init(&value);
    if (!rt_program_pop(program, &value))
        return rt_error(runtime, insn->id, "表达式值栈损坏");
    MKValue result;
    mk_value_init(&result);
    CCBOOL ok = rt_apply_unary(
        runtime, insn->id, insn->unary, &value, &result
    );
    mk_value_clear(&value);
    if (!ok)
    {
        mk_value_clear(&result);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &result))
    {
        mk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_binary(
    MKRuntime *runtime, MKExprProgram *program,
    const MKExprInsn *insn
)
{
    MKValue right;
    MKValue left;
    mk_value_init(&right);
    mk_value_init(&left);
    if (!rt_program_pop(program, &right) ||
        !rt_program_pop(program, &left))
    {
        mk_value_clear(&right);
        mk_value_clear(&left);
        return rt_error(runtime, insn->id, "表达式值栈损坏");
    }
    MKValue result;
    mk_value_init(&result);
    CCBOOL ok = rt_apply_binary(
        runtime, insn->id, insn->binary, &left, &right, &result
    );
    mk_value_clear(&left);
    mk_value_clear(&right);
    if (!ok)
    {
        mk_value_clear(&result);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &result))
    {
        mk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_call(
    MKRuntime *runtime, MKExprProgram *program,
    const MKExprInsn *insn, CCBOOL *suspended,
    const MKFunctionInfo **call_function,
    MKValue **call_args, CCUINT32 *call_argc
)
{
    MKValue *args = NULL;
    if (insn->argc > 0)
    {
        args = ccalloc(
            NULL, (CCUINT64)insn->argc * sizeof(MKValue),
            MK_RT_COMMENT
        );
        if (!args)
            return rt_error(runtime, insn->id, "内存分配失败");
        for (CCUINT32 i = 0; i < insn->argc; i++)
            mk_value_init(&args[i]);
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

    MKValue result;
    mk_value_init(&result);
    CCBOOL ok = rt_invoke_call(
        runtime, insn->id, NULL, insn->builtin,
        args, insn->argc, &result
    );
    rt_call_args_clear(args, insn->argc);
    if (!ok)
    {
        mk_value_clear(&result);
        return CCFALSE;
    }
    if (!rt_program_push(runtime, program, &result))
    {
        mk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_index(
    MKRuntime *runtime, MKExprProgram *program,
    const MKExprInsn *insn
)
{
    MKValue index;
    MKValue base;
    mk_value_init(&index);
    mk_value_init(&base);
    if (!rt_program_pop(program, &index) ||
        !rt_program_pop(program, &base))
    {
        mk_value_clear(&index);
        mk_value_clear(&base);
        return rt_error(runtime, insn->id, "表达式值栈损坏");
    }
    if (base.type != MK_VALUE_LIST ||
        index.type != MK_VALUE_INT)
    {
        mk_value_clear(&index);
        mk_value_clear(&base);
        return rt_error(runtime, insn->id, "索引需要 list[int]");
    }
    CCINT64 at = index.as.integer;
    if (at < 0 || (CCUINT64)at >= mk_list_count(base.as.list))
    {
        mk_value_clear(&index);
        mk_value_clear(&base);
        return rt_error(runtime, insn->id, "列表索引越界");
    }
    MKValue result;
    mk_value_init(&result);
    CCBOOL ok = mk_value_copy(
        &result, mk_list_at(base.as.list, (CCUINT32)at)
    );
    mk_value_clear(&index);
    mk_value_clear(&base);
    if (!ok)
        return rt_error(runtime, insn->id, "索引值复制失败");
    if (!rt_program_push(runtime, program, &result))
    {
        mk_value_clear(&result);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL rt_expr_program_run(
    MKRuntime *runtime, MKExprProgram *program, MKValue *input,
    const MKFunctionInfo **call_function, MKValue **call_args,
    CCUINT32 *call_argc, MKValue *out, CCBOOL *suspended
)
{
    *suspended = CCFALSE;
    if (input && !rt_program_push(runtime, program, input))
        return CCFALSE;

    MKExprInsn *insns = (MKExprInsn *)program->insns.array;
    CCUINT32 count = program->insns.size /
        (CCUINT32)sizeof(MKExprInsn);
    while (program->ip < count)
    {
        MKExprInsn *insn = &insns[program->ip++];
        CCBOOL ok = CCFALSE;
        switch (insn->kind)
        {
        case MK_EXPR_INS_VALUE:
            ok = rt_expr_program_value(runtime, program, insn);
            break;
        case MK_EXPR_INS_UNARY:
            ok = rt_expr_program_unary(runtime, program, insn);
            break;
        case MK_EXPR_INS_BINARY:
            ok = rt_expr_program_binary(runtime, program, insn);
            break;
        case MK_EXPR_INS_CALL:
            ok = rt_expr_program_call(
                runtime, program, insn, suspended,
                call_function, call_args, call_argc
            );
            break;
        case MK_EXPR_INS_INDEX:
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

    if (program->values.size != (CCUINT32)sizeof(MKValue))
        return rt_error(runtime, MK_AST_NODE_NIL, "表达式值栈不平衡");
    mk_value_init(out);
    if (!rt_program_pop(program, out))
        return rt_error(runtime, MK_AST_NODE_NIL, "表达式结果缺失");
    return CCTRUE;
}

static CCBOOL mk_runtime_call_function_inner(
    MKRuntime *runtime,
    const MKFunctionInfo *function,
    const MKValue *args, CCUINT32 argc,
    MKValue *out
);

CCBOOL mk_runtime_call_function(
    MKRuntime *runtime,
    const MKFunctionInfo *function,
    const MKValue *args, CCUINT32 argc,
    MKValue *out
)
{
    if (!runtime || !function || !out)
        return CCFALSE;
    if (!mk_exec_stack_check((size_t)64 * 1024))
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
    CCBOOL ok = mk_runtime_call_function_inner(
        runtime, function, args, argc, out
    );
    runtime->call_depth--;
    return ok;
}





static void rt_call_args_clear(MKValue *args, CCUINT32 argc)
{
    if (!args)
        return;
    for (CCUINT32 i = 0; i < argc; i++)
        mk_value_clear(&args[i]);
    ccalloc(args, 0, NULL);
}



typedef enum mk_script_frame_kind {
    MK_SCRIPT_BLOCK = 0,
    MK_SCRIPT_IF,
    MK_SCRIPT_WHILE,
    MK_SCRIPT_FOR,
    MK_SCRIPT_SIMPLE
} MKScriptFrameKind;

typedef enum mk_script_flow {
    MK_SCRIPT_FLOW_NORMAL = 0,
    MK_SCRIPT_FLOW_BREAK,
    MK_SCRIPT_FLOW_CONTINUE
} MKScriptFlow;

typedef struct mk_script_frame {
    MKScriptFrameKind kind;
    MKASTNodeId id;
    MKEnv *env;
    CCBOOL owns_env;
    MKEnv *loop_env;
    MKASTNodeId next;
    CCUINT32 stage;
    MKASTNodeId body;
    MKValue iterable;
    const char *name;
    CCUINT32 name_len;
    CCUINT32 index;
    CCUINT32 count;
} MKScriptFrame;

typedef struct mk_script {
    MKRuntime *runtime;
    const MKFunctionInfo *function;
    MKEnv *env;
    CCDyn frames;
    MKExecResult result;
    MKExprProgram *pending;
    const MKFunctionInfo *call_function;
    MKValue *call_args;
    CCUINT32 call_argc;
    MKScriptFlow flow;
    CCBOOL done;
} MKScript;

static MKScriptFrame *rt_script_top(MKScript *script)
{
    if (!script || script->frames.size < (CCUINT32)sizeof(MKScriptFrame))
        return NULL;
    return &((MKScriptFrame *)script->frames.array)
        [script->frames.size / (CCUINT32)sizeof(MKScriptFrame) - 1];
}

static void rt_script_frame_clear(MKScriptFrame *frame)
{
    if (!frame)
        return;
    mk_value_clear(&frame->iterable);
    if (frame->loop_env)
        env_destroy(frame->loop_env);
    if (frame->owns_env && frame->env)
        env_destroy(frame->env);
    memset(frame, 0, sizeof(*frame));
}

static void rt_script_clear(MKScript *script)
{
    if (!script)
        return;
    MKScriptFrame *frames = (MKScriptFrame *)script->frames.array;
    CCUINT32 count = script->frames.size /
        (CCUINT32)sizeof(MKScriptFrame);
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
    MKRuntime *runtime, MKScript *script,
    MKScriptFrameKind kind, MKASTNodeId id,
    MKEnv *env, CCBOOL owns_env, MKASTNodeId next
)
{
    MKScriptFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = kind;
    frame.id = id;
    frame.env = env;
    frame.owns_env = owns_env;
    frame.next = next;
    if (!mk_dyn_push(
            &script->frames, (CCUINT32)sizeof(MKScriptFrame),
            (CCINT32)sizeof(MKScriptFrame), &frame
        ))
        return rt_error(runtime, id, "内存分配失败");
    return CCTRUE;
}

typedef enum mk_script_status {
    MK_SCRIPT_RUN = 0,
    MK_SCRIPT_WAIT,
    MK_SCRIPT_DONE,
    MK_SCRIPT_ERROR
} MKScriptStatus;

static MKASTNodeId rt_script_child(
    MKRuntime *runtime, MKASTNodeId id, CCUINT32 index
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return MK_AST_NODE_NIL;
    MKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < index && child != MK_AST_NODE_NIL; i++)
    {
        const MKASTNode *child_node =
            mk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : MK_AST_NODE_NIL;
    }
    return child;
}

static MKASTNodeId rt_script_var_init(
    MKRuntime *runtime, MKASTNodeId id
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return MK_AST_NODE_NIL;
    MKASTNodeId child = node->first_child;
    for (CCUINT32 i = 0; i < 1 && child != MK_AST_NODE_NIL; i++)
    {
        const MKASTNode *child_node =
            mk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : MK_AST_NODE_NIL;
    }
    return child;
}

static MKASTNodeId rt_script_assign_value(
    MKRuntime *runtime, MKASTNodeId id
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return MK_AST_NODE_NIL;
    MKASTNodeId target = node->first_child;
    const MKASTNode *target_node =
        mk_ast_node(runtime->semantic->ast, target);
    return target_node ? target_node->next_sibling : MK_AST_NODE_NIL;
}

static CCBOOL rt_script_push_statement(
    MKRuntime *runtime, MKScript *script,
    MKASTNodeId id, MKEnv *env
);

static MKScriptStatus rt_script_start_expr(
    MKScript *script, MKASTNodeId expr_id,
    MKEnv *env, MKValue *out
)
{
    if (expr_id == MK_AST_NODE_NIL)
    {
        mk_value_init(out);
        return MK_SCRIPT_RUN;
    }

    CCBOOL has_call = CCFALSE;
    if (!rt_expr_scan_user_call(script->runtime, expr_id, &has_call))
        return MK_SCRIPT_ERROR;
    if (!has_call)
    {
        mk_value_init(out);
        return rt_eval_expr(
            script->runtime, expr_id, env, out
        ) ? MK_SCRIPT_RUN : MK_SCRIPT_ERROR;
    }

    MKExprProgram *program = ccalloc(
        NULL, sizeof(MKExprProgram), MK_RT_COMMENT
    );
    if (!program)
    {
        (void)rt_error(
            script->runtime, expr_id, "内存分配失败"
        );
        return MK_SCRIPT_ERROR;
    }
    memset(program, 0, sizeof(*program));
    program->env = env;
    if (!rt_compile_expr(script->runtime, expr_id, &program->insns))
    {
        rt_expr_program_clear(program);
        ccalloc(program, 0, NULL);
        return MK_SCRIPT_ERROR;
    }

    const MKFunctionInfo *call_function = NULL;
    MKValue *call_args = NULL;
    CCUINT32 call_argc = 0;
    CCBOOL suspended = CCFALSE;
    mk_value_init(out);
    if (!rt_expr_program_run(
            script->runtime, program, NULL, &call_function,
            &call_args, &call_argc, out, &suspended
        ))
    {
        rt_expr_program_clear(program);
        ccalloc(program, 0, NULL);
        return MK_SCRIPT_ERROR;
    }
    if (suspended)
    {
        script->pending = program;
        script->call_function = call_function;
        script->call_args = call_args;
        script->call_argc = call_argc;
        return MK_SCRIPT_WAIT;
    }
    rt_expr_program_clear(program);
    ccalloc(program, 0, NULL);
    return MK_SCRIPT_RUN;
}

static MKScriptStatus rt_script_resume_expr(
    MKScript *script, MKValue *value, MKValue *out
)
{
    if (!script->pending)
        return MK_SCRIPT_ERROR;
    const MKFunctionInfo *call_function = NULL;
    MKValue *call_args = NULL;
    CCUINT32 call_argc = 0;
    CCBOOL suspended = CCFALSE;
    mk_value_init(out);
    if (!rt_expr_program_run(
            script->runtime, script->pending, value,
            &call_function, &call_args, &call_argc, out, &suspended
        ))
        return MK_SCRIPT_ERROR;
    if (suspended)
    {
        script->call_function = call_function;
        script->call_args = call_args;
        script->call_argc = call_argc;
        return MK_SCRIPT_WAIT;
    }
    rt_expr_program_clear(script->pending);
    ccalloc(script->pending, 0, NULL);
    script->pending = NULL;
    return MK_SCRIPT_RUN;
}

static MKScriptStatus rt_script_apply_if(
    MKScript *script, MKValue *value
)
{
    if (value->type != MK_VALUE_BOOL)
    {
        mk_value_clear(value);
        (void)rt_error(
            script->runtime, rt_script_top(script)->id,
            "if 条件必须是 bool"
        );
        return MK_SCRIPT_ERROR;
    }
    CCBOOL choose_then = value->as.boolean;
    mk_value_clear(value);

    MKScriptFrame *frame = rt_script_top(script);
    if (!frame)
        return MK_SCRIPT_ERROR;
    MKASTNodeId condition = rt_script_child(
        script->runtime, frame->id, 0
    );
    MKASTNodeId then_id = MK_AST_NODE_NIL;
    MKASTNodeId else_id = MK_AST_NODE_NIL;
    const MKASTNode *condition_node =
        mk_ast_node(script->runtime->semantic->ast, condition);
    if (condition_node)
    {
        then_id = condition_node->next_sibling;
        const MKASTNode *then_node =
            mk_ast_node(script->runtime->semantic->ast, then_id);
        else_id = then_node ? then_node->next_sibling : MK_AST_NODE_NIL;
    }
    MKASTNodeId branch = choose_then ? then_id : else_id;
    frame->stage = 1;
    if (branch == MK_AST_NODE_NIL)
    {
        MKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(MKScriptFrame),
                (CCINT32)sizeof(MKScriptFrame), &popped
            ) != 0)
            return MK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
        return MK_SCRIPT_RUN;
    }
    return rt_script_push_statement(
        script->runtime, script, branch, frame->env
    ) ? MK_SCRIPT_RUN : MK_SCRIPT_ERROR;
}

static MKScriptStatus rt_script_apply_simple(
    MKScript *script, MKValue *value
)
{
    MKScriptFrame *frame = rt_script_top(script);
    if (!frame)
    {
        mk_value_clear(value);
        return MK_SCRIPT_ERROR;
    }
    const MKASTNode *node = mk_ast_node(
        script->runtime->semantic->ast, frame->id
    );
    if (!node)
    {
        mk_value_clear(value);
        return MK_SCRIPT_ERROR;
    }

    CCBOOL ok = CCTRUE;
    if (node->kind == MK_AST_VAR_DECL)
        ok = rt_var_decl_apply(
            script->runtime, frame->id, frame->env, value
        );
    else if (node->kind == MK_AST_ASSIGN)
        ok = rt_assign_apply(
            script->runtime, frame->id, frame->env, value
        );
    else if (node->kind == MK_AST_EXPR_STMT)
        mk_value_clear(value);
    else if (node->kind == MK_AST_RETURN)
    {
        script->result.kind = MK_EXEC_RETURN;
        script->result.value = *value;
        mk_value_init(value);
        script->done = CCTRUE;
    }
    else
    {
        mk_value_clear(value);
        ok = rt_error(
            script->runtime, frame->id, "不支持的语句节点"
        );
    }
    if (!ok)
        return MK_SCRIPT_ERROR;

    if (!script->done)
    {
        MKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(MKScriptFrame),
                (CCINT32)sizeof(MKScriptFrame), &popped
            ) != 0)
            return MK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
    }
    return MK_SCRIPT_RUN;
}

static MKScriptStatus rt_script_start_simple(MKScript *script)
{
    MKScriptFrame *frame = rt_script_top(script);
    if (!frame)
        return MK_SCRIPT_ERROR;
    const MKASTNode *node = mk_ast_node(
        script->runtime->semantic->ast, frame->id
    );
    if (!node)
        return MK_SCRIPT_ERROR;

    if (node->kind == MK_AST_BREAK ||
        node->kind == MK_AST_CONTINUE)
    {
        script->flow = (node->kind == MK_AST_BREAK)
            ? MK_SCRIPT_FLOW_BREAK : MK_SCRIPT_FLOW_CONTINUE;
        MKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(MKScriptFrame),
                (CCINT32)sizeof(MKScriptFrame), &popped
            ) != 0)
            return MK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
        return MK_SCRIPT_RUN;
    }

    MKASTNodeId expr_id = MK_AST_NODE_NIL;
    if (node->kind == MK_AST_VAR_DECL)
        expr_id = rt_script_var_init(script->runtime, frame->id);
    else if (node->kind == MK_AST_ASSIGN)
        expr_id = rt_script_assign_value(script->runtime, frame->id);
    else if (node->kind == MK_AST_EXPR_STMT ||
             node->kind == MK_AST_RETURN)
        expr_id = frame->id == MK_AST_NODE_NIL
            ? MK_AST_NODE_NIL : node->first_child;

    if (node->kind == MK_AST_RETURN && expr_id == MK_AST_NODE_NIL)
    {
        MKScriptFrame popped;
        memset(&popped, 0, sizeof(popped));
        if (cc_dyn_core_pop(
                &script->frames, (CCUINT32)sizeof(MKScriptFrame),
                (CCINT32)sizeof(MKScriptFrame), &popped
            ) != 0)
            return MK_SCRIPT_ERROR;
        rt_script_frame_clear(&popped);
        script->result.kind = MK_EXEC_NORMAL;
        script->done = CCTRUE;
        return MK_SCRIPT_RUN;
    }

    if (expr_id == MK_AST_NODE_NIL)
    {
        (void)rt_error(
            script->runtime, frame->id, "语句表达式缺失"
        );
        return MK_SCRIPT_ERROR;
    }

    CCBOOL has_call = CCFALSE;
    if (!rt_expr_scan_user_call(
            script->runtime, expr_id, &has_call
        ))
        return MK_SCRIPT_ERROR;
    if (!has_call)
    {
        CCBOOL ok = CCTRUE;
        if (node->kind == MK_AST_VAR_DECL)
            ok = rt_eval_var_decl(
                script->runtime, frame->id, frame->env
            );
        else if (node->kind == MK_AST_ASSIGN)
            ok = rt_eval_assign(
                script->runtime, frame->id, frame->env
            );
        else if (node->kind == MK_AST_EXPR_STMT)
        {
            MKValue value;
            mk_value_init(&value);
            ok = rt_eval_expr(
                script->runtime, expr_id, frame->env, &value
            );
            mk_value_clear(&value);
        }
        else if (node->kind == MK_AST_RETURN)
        {
            MKValue value;
            mk_value_init(&value);
            ok = rt_eval_expr(
                script->runtime, expr_id, frame->env, &value
            );
            if (ok)
            {
                script->result.kind = MK_EXEC_RETURN;
                script->result.value = value;
                mk_value_init(&value);
                script->done = CCTRUE;
            }
            mk_value_clear(&value);
        }
        if (!ok)
            return MK_SCRIPT_ERROR;
        if (!script->done)
        {
            MKScriptFrame popped;
            memset(&popped, 0, sizeof(popped));
            if (cc_dyn_core_pop(
                    &script->frames, (CCUINT32)sizeof(MKScriptFrame),
                    (CCINT32)sizeof(MKScriptFrame), &popped
                ) != 0)
                return MK_SCRIPT_ERROR;
            rt_script_frame_clear(&popped);
        }
        return MK_SCRIPT_RUN;
    }

    frame->stage = 1;
    MKValue value;
    MKScriptStatus status = rt_script_start_expr(
        script, expr_id, frame->env, &value
    );
    if (status != MK_SCRIPT_RUN)
        return status;
    return rt_script_apply_simple(script, &value);
}

static MKScriptStatus rt_script_pop_top(MKScript *script)
{
    MKScriptFrame popped;
    memset(&popped, 0, sizeof(popped));
    if (cc_dyn_core_pop(
            &script->frames, (CCUINT32)sizeof(MKScriptFrame),
            (CCINT32)sizeof(MKScriptFrame), &popped
        ) != 0)
        return MK_SCRIPT_ERROR;
    rt_script_frame_clear(&popped);
    return MK_SCRIPT_RUN;
}

static MKScriptStatus rt_script_apply_while(
    MKScript *script, MKValue *value
)
{
    if (value->type != MK_VALUE_BOOL)
    {
        mk_value_clear(value);
        (void)rt_error(
            script->runtime, rt_script_top(script)->id,
            "while 条件必须是 bool"
        );
        return MK_SCRIPT_ERROR;
    }
    CCBOOL run = value->as.boolean;
    mk_value_clear(value);
    MKScriptFrame *frame = rt_script_top(script);
    if (!frame)
        return MK_SCRIPT_ERROR;
    if (!run)
        return rt_script_pop_top(script);
    if (frame->body == MK_AST_NODE_NIL)
    {
        (void)rt_error(
            script->runtime, frame->id, "while 循环体缺失"
        );
        return MK_SCRIPT_ERROR;
    }
    frame->stage = 1;
    MKASTNodeId body = frame->body;
    MKEnv *env = frame->env;
    return rt_script_push_statement(
        script->runtime, script, body, env
    ) ? MK_SCRIPT_RUN : MK_SCRIPT_ERROR;
}

static MKScriptStatus rt_script_for_next(MKScript *script)
{
    MKScriptFrame *frame = rt_script_top(script);
    if (!frame || frame->kind != MK_SCRIPT_FOR)
        return MK_SCRIPT_ERROR;
    if (frame->index >= frame->count)
        return rt_script_pop_top(script);

    const MKValue *item = mk_list_at(
        frame->iterable.as.list, frame->index
    );
    if (!item)
    {
        (void)rt_error(
            script->runtime, frame->id, "for-in 元素缺失"
        );
        return MK_SCRIPT_ERROR;
    }
    MKEnvVar *slot = env_slot(
        frame->loop_env, frame->name, frame->name_len
    );
    if (!slot)
    {
        if (!env_define(
                script->runtime, frame->loop_env,
                frame->name, frame->name_len, item,
                CCFALSE, frame->id
            ))
            return MK_SCRIPT_ERROR;
    }
    else if (!mk_value_copy(&slot->value, item))
    {
        (void)rt_error(
            script->runtime, frame->id, "循环变量赋值失败"
        );
        return MK_SCRIPT_ERROR;
    }
    if (frame->body == MK_AST_NODE_NIL)
    {
        (void)rt_error(
            script->runtime, frame->id, "for-in 循环体缺失"
        );
        return MK_SCRIPT_ERROR;
    }
    frame->stage = 2;
    MKASTNodeId body = frame->body;
    MKEnv *env = frame->loop_env;
    return rt_script_push_statement(
        script->runtime, script, body, env
    ) ? MK_SCRIPT_RUN : MK_SCRIPT_ERROR;
}

static MKScriptStatus rt_script_apply_for(
    MKScript *script, MKValue *value
)
{
    MKScriptFrame *frame = rt_script_top(script);
    if (!frame)
    {
        mk_value_clear(value);
        return MK_SCRIPT_ERROR;
    }
    const MKASTNode *node = mk_ast_node(
        script->runtime->semantic->ast, frame->id
    );
    if (!node)
    {
        mk_value_clear(value);
        return MK_SCRIPT_ERROR;
    }
    if (value->type != MK_VALUE_LIST)
    {
        mk_value_clear(value);
        (void)rt_error(
            script->runtime, frame->id, "for-in 需要 list"
        );
        return MK_SCRIPT_ERROR;
    }
    CCUINT32 name_len = 0;
    const char *name = mk_ast_string(
        script->runtime->semantic->ast,
        node->as.for_in.name, &name_len
    );
    if (!name)
    {
        mk_value_clear(value);
        (void)rt_error(
            script->runtime, frame->id, "for-in 变量名无效"
        );
        return MK_SCRIPT_ERROR;
    }
    frame->loop_env = env_create(frame->env);
    if (!frame->loop_env)
    {
        mk_value_clear(value);
        (void)rt_error(
            script->runtime, frame->id, "内存分配失败"
        );
        return MK_SCRIPT_ERROR;
    }
    frame->iterable = *value;
    mk_value_init(value);
    frame->name = name;
    frame->name_len = name_len;
    frame->count = mk_list_count(frame->iterable.as.list);
    frame->index = 0;
    frame->stage = 1;

    MKASTNodeId type_id = node->first_child;
    const MKASTNode *type_node =
        mk_ast_node(script->runtime->semantic->ast, type_id);
    MKASTNodeId iterable_id = type_node
        ? type_node->next_sibling : MK_AST_NODE_NIL;
    const MKASTNode *iterable_node =
        mk_ast_node(script->runtime->semantic->ast, iterable_id);
    frame->body = iterable_node
        ? iterable_node->next_sibling : MK_AST_NODE_NIL;
    return rt_script_for_next(script);
}

static MKScriptStatus rt_script_apply_value(
    MKScript *script, MKValue *value
)
{
    MKScriptFrame *frame = rt_script_top(script);
    if (!frame)
    {
        mk_value_clear(value);
        return MK_SCRIPT_ERROR;
    }
    if (frame->kind == MK_SCRIPT_IF)
        return rt_script_apply_if(script, value);
    if (frame->kind == MK_SCRIPT_WHILE)
        return rt_script_apply_while(script, value);
    if (frame->kind == MK_SCRIPT_FOR)
        return rt_script_apply_for(script, value);
    if (frame->kind == MK_SCRIPT_SIMPLE)
        return rt_script_apply_simple(script, value);
    mk_value_clear(value);
    (void)rt_error(
        script->runtime, frame->id, "脚本帧类型错误"
    );
    return MK_SCRIPT_ERROR;
}

static CCBOOL rt_script_push_statement(
    MKRuntime *runtime, MKScript *script,
    MKASTNodeId id, MKEnv *env
)
{
    const MKASTNode *node =
        mk_ast_node(runtime->semantic->ast, id);
    if (!node)
        return rt_error(runtime, id, "语句节点缺失");
    if (node->kind == MK_AST_BLOCK)
    {
        MKEnv *block_env = env_create(env);
        if (!block_env)
            return rt_error(runtime, id, "内存分配失败");
        return rt_script_push_frame(
            runtime, script, MK_SCRIPT_BLOCK, id,
            block_env, CCTRUE, node->first_child
        );
    }
    if (node->kind == MK_AST_IF)
        return rt_script_push_frame(
            runtime, script, MK_SCRIPT_IF, id,
            env, CCFALSE, MK_AST_NODE_NIL
        );
    if (node->kind == MK_AST_WHILE)
    {
        MKASTNodeId condition = node->first_child;
        const MKASTNode *condition_node =
            mk_ast_node(runtime->semantic->ast, condition);
        MKASTNodeId body = condition_node
            ? condition_node->next_sibling : MK_AST_NODE_NIL;
        if (!rt_script_push_frame(
                runtime, script, MK_SCRIPT_WHILE, id,
                env, CCFALSE, MK_AST_NODE_NIL
            ))
            return CCFALSE;
        MKScriptFrame *frame = rt_script_top(script);
        if (!frame)
            return rt_error(runtime, id, "脚本帧缺失");
        frame->body = body;
        return CCTRUE;
    }
    if (node->kind == MK_AST_FOR_IN)
        return rt_script_push_frame(
            runtime, script, MK_SCRIPT_FOR, id,
            env, CCFALSE, MK_AST_NODE_NIL
        );
    if (node->kind == MK_AST_VAR_DECL ||
        node->kind == MK_AST_ASSIGN ||
        node->kind == MK_AST_EXPR_STMT ||
        node->kind == MK_AST_RETURN ||
        node->kind == MK_AST_BREAK ||
        node->kind == MK_AST_CONTINUE)
        return rt_script_push_frame(
            runtime, script, MK_SCRIPT_SIMPLE, id,
            env, CCFALSE, MK_AST_NODE_NIL
        );
    return rt_error(runtime, id, "不支持的语句节点");
}

static MKScriptStatus rt_script_run(MKScript *script, MKValue *input)
{
    if (script->done)
        return MK_SCRIPT_DONE;
    if (script->pending)
    {
        if (!input)
        {
            (void)rt_error(
                script->runtime, script->function->decl,
                "脚本缺少恢复值：%s", script->function->name.data
            );
            return MK_SCRIPT_ERROR;
        }
        MKValue value;
        MKScriptStatus status = rt_script_resume_expr(
            script, input, &value
        );
        if (status != MK_SCRIPT_RUN)
            return status;
        status = rt_script_apply_value(script, &value);
        if (status != MK_SCRIPT_RUN)
            return status;
    }

    for (;;)
    {
        if (script->done)
            return MK_SCRIPT_DONE;
        MKScriptFrame *frame = rt_script_top(script);
        if (!frame)
        {
            script->result.kind = MK_EXEC_NORMAL;
            script->done = CCTRUE;
            return MK_SCRIPT_DONE;
        }

        if (script->flow != MK_SCRIPT_FLOW_NORMAL)
        {
            while (frame && frame->kind != MK_SCRIPT_WHILE &&
                   frame->kind != MK_SCRIPT_FOR)
            {
                if (rt_script_pop_top(script) != MK_SCRIPT_RUN)
                    return MK_SCRIPT_ERROR;
                frame = rt_script_top(script);
            }
            if (!frame)
            {
                (void)rt_error(
                    script->runtime, script->function->decl,
                    "break/continue 不在循环内"
                );
                return MK_SCRIPT_ERROR;
            }
            if (script->flow == MK_SCRIPT_FLOW_BREAK)
            {
                script->flow = MK_SCRIPT_FLOW_NORMAL;
                if (rt_script_pop_top(script) != MK_SCRIPT_RUN)
                    return MK_SCRIPT_ERROR;
                continue;
            }
            frame->stage = (frame->kind == MK_SCRIPT_WHILE) ? 0 : 2;
            script->flow = MK_SCRIPT_FLOW_NORMAL;
            continue;
        }

        if (frame->kind == MK_SCRIPT_BLOCK)
        {
            if (frame->next == MK_AST_NODE_NIL)
            {
                MKScriptFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (cc_dyn_core_pop(
                        &script->frames,
                        (CCUINT32)sizeof(MKScriptFrame),
                        (CCINT32)sizeof(MKScriptFrame), &popped
                    ) != 0)
                    return MK_SCRIPT_ERROR;
                rt_script_frame_clear(&popped);
                continue;
            }
            MKASTNodeId child = frame->next;
            const MKASTNode *child_node = mk_ast_node(
                script->runtime->semantic->ast, child
            );
            frame->next = child_node
                ? child_node->next_sibling : MK_AST_NODE_NIL;
            if (!rt_script_push_statement(
                    script->runtime, script, child, frame->env
                ))
                return MK_SCRIPT_ERROR;
            continue;
        }

        if (frame->kind == MK_SCRIPT_WHILE)
        {
            if (frame->stage == 1)
            {
                frame->stage = 0;
                continue;
            }
            MKASTNodeId condition = rt_script_child(
                script->runtime, frame->id, 0
            );
            MKValue value;
            MKScriptStatus status = rt_script_start_expr(
                script, condition, frame->env, &value
            );
            if (status != MK_SCRIPT_RUN)
                return status;
            status = rt_script_apply_while(script, &value);
            if (status != MK_SCRIPT_RUN)
                return status;
            continue;
        }

        if (frame->kind == MK_SCRIPT_FOR)
        {
            if (frame->stage == 2)
            {
                frame->index++;
                frame->stage = 1;
                continue;
            }
            if (frame->stage == 1)
            {
                MKScriptStatus status = rt_script_for_next(script);
                if (status != MK_SCRIPT_RUN)
                    return status;
                continue;
            }
            MKASTNodeId type_id = rt_script_child(
                script->runtime, frame->id, 0
            );
            MKASTNodeId iterable_id = rt_script_child(
                script->runtime, frame->id, 1
            );
            (void)type_id;
            MKValue value;
            MKScriptStatus status = rt_script_start_expr(
                script, iterable_id, frame->env, &value
            );
            if (status != MK_SCRIPT_RUN)
                return status;
            status = rt_script_apply_for(script, &value);
            if (status != MK_SCRIPT_RUN)
                return status;
            continue;
        }

        if (frame->kind == MK_SCRIPT_IF)
        {
            if (frame->stage == 1)
            {
                MKScriptFrame popped;
                memset(&popped, 0, sizeof(popped));
                if (cc_dyn_core_pop(
                        &script->frames,
                        (CCUINT32)sizeof(MKScriptFrame),
                        (CCINT32)sizeof(MKScriptFrame), &popped
                    ) != 0)
                    return MK_SCRIPT_ERROR;
                rt_script_frame_clear(&popped);
                continue;
            }
            MKASTNodeId condition = rt_script_child(
                script->runtime, frame->id, 0
            );
            MKValue value;
            MKScriptStatus status = rt_script_start_expr(
                script, condition, frame->env, &value
            );
            if (status != MK_SCRIPT_RUN)
            {
                if (status == MK_SCRIPT_WAIT)
                    frame->stage = 1;
                return status;
            }
            status = rt_script_apply_if(script, &value);
            if (status != MK_SCRIPT_RUN)
                return status;
            continue;
        }

        MKScriptStatus status = rt_script_start_simple(script);
        if (status != MK_SCRIPT_RUN)
            return status;
    }
}

static MKScript *rt_script_create(
    MKRuntime *runtime, const MKFunctionInfo *function,
    const MKValue *args, CCUINT32 argc
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

    MKScript *script = ccalloc(
        NULL, sizeof(MKScript), MK_RT_COMMENT
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

    const MKASTNode *decl =
        mk_ast_node(runtime->semantic->ast, function->decl);
    if (!decl)
    {
        rt_script_clear(script);
        ccalloc(script, 0, NULL);
        (void)rt_error(runtime, function->decl, "函数声明缺失");
        return NULL;
    }

    MKASTNodeId param = decl->first_child;
    const MKASTNode *param_type =
        mk_ast_node(runtime->semantic->ast, param);
    param = param_type ? param_type->next_sibling : MK_AST_NODE_NIL;
    for (CCUINT32 i = 0; i < argc; i++)
    {
        const MKASTNode *param_node =
            mk_ast_node(runtime->semantic->ast, param);
        if (!param_node || param_node->kind != MK_AST_PARAM)
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
        const char *name = mk_ast_string(
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
        const MKASTNode *next = mk_ast_node(
            runtime->semantic->ast, param_node->next_sibling
        );
        param = next ? param_node->next_sibling : MK_AST_NODE_NIL;
    }

    MKASTNodeId body = MK_AST_NODE_NIL;
    MKASTNodeId child = decl->first_child;
    for (CCUINT32 i = 0; i < function->param_count + 1; i++)
    {
        const MKASTNode *child_node =
            mk_ast_node(runtime->semantic->ast, child);
        child = child_node ? child_node->next_sibling : MK_AST_NODE_NIL;
    }
    body = child;
    const MKASTNode *body_node =
        mk_ast_node(runtime->semantic->ast, body);
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
            runtime, script, MK_SCRIPT_BLOCK, body,
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
    MKRuntime *runtime, MKScript *script,
    CCDyn *scripts, CCBOOL *pushed
)
{
    *pushed = CCFALSE;
    if (!script->call_function)
        return CCTRUE;

    const MKFunctionInfo *callee = script->call_function;
    MKValue *call_args = script->call_args;
    CCUINT32 call_argc = script->call_argc;
    script->call_function = NULL;
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

    MKRuntime *target = runtime;
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
        if (!mk_runtime_init_globals(target))
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

    MKScript *child = rt_script_create(
        target, callee, call_args, call_argc
    );
    rt_call_args_clear(call_args, call_argc);
    if (!child)
        return CCFALSE;
    target->call_depth++;
    if (!mk_dyn_push(
            scripts, (CCUINT32)sizeof(MKScript *),
            (CCINT32)sizeof(MKScript *), &child
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
    MKRuntime *runtime, const MKFunctionInfo *function,
    const MKValue *args, CCUINT32 argc, MKValue *out
)
{
    CCDyn scripts;
    memset(&scripts, 0, sizeof(scripts));
    MKScript *root = rt_script_create(
        runtime, function, args, argc
    );
    if (!root)
        return CCFALSE;
    if (!mk_dyn_push(
            &scripts, (CCUINT32)sizeof(MKScript *),
            (CCINT32)sizeof(MKScript *), &root
        ))
    {
        rt_script_clear(root);
        ccalloc(root, 0, NULL);
        return rt_error(runtime, function->decl, "内存分配失败");
    }

    CCBOOL ok = CCFALSE;
    CCBOOL have_value = CCFALSE;
    MKValue value;
    mk_value_init(&value);

    for (;;)
    {
        CCUINT32 count = scripts.size /
            (CCUINT32)sizeof(MKScript *);
        MKScript **items = (MKScript **)scripts.array;
        MKScript *script = items[count - 1];

        CCBOOL pushed = CCFALSE;
        if (!rt_script_start_call(runtime, script, &scripts, &pushed))
            goto Finished;
        if (pushed)
            continue;

        MKScriptStatus status = rt_script_run(script, NULL);
        if (status == MK_SCRIPT_ERROR)
        {
            if (!mk_diagnostic_has(runtime->diagnostic))
                (void)rt_error(
                    runtime, script->function->decl,
                    "脚本执行失败：%s",
                    script->function->name.data
                );
            goto Finished;
        }
        if (status == MK_SCRIPT_WAIT)
            continue;

        /* 脚本完成：校验收敛并回传调用者 */
        if (script->result.kind == MK_EXEC_RETURN)
        {
            if (!rt_expect_value(
                    runtime, script->function->decl,
                    &script->result.value,
                    &script->function->return_type
                ))
                goto Finished;
            mk_value_clear(&value);
            value = script->result.value;
            mk_value_init(&script->result.value);
        }
        else if (script->result.kind == MK_EXEC_NORMAL)
        {
            if (script->function->return_type.kind != MK_VALUE_VOID)
            {
                (void)rt_error(
                    runtime, script->function->decl,
                    "非 void 函数 %s 没有返回值",
                    script->function->name.data
                );
                goto Finished;
            }
            mk_value_clear(&value);
            (void)mk_value_set_void(&value);
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

        MKRuntime *script_runtime = script->runtime;
        rt_script_clear(script);
        ccalloc(script, 0, NULL);
        scripts.size -= (CCUINT32)sizeof(MKScript *);
        script_runtime->call_depth--;

        count = scripts.size / (CCUINT32)sizeof(MKScript *);
        if (count == 0)
        {
            *out = value;
            mk_value_init(&value);
            ok = CCTRUE;
            goto Finished;
        }

        MKScript *caller = ((MKScript **)scripts.array)[count - 1];
        MKValue input = value;
        mk_value_init(&value);
        status = rt_script_run(caller, &input);
        if (status == MK_SCRIPT_ERROR)
            goto Finished;
        have_value = CCFALSE;
    }

Finished:
    {
        CCUINT32 count = scripts.size / (CCUINT32)sizeof(MKScript *);
        MKScript **items = (MKScript **)scripts.array;
        for (CCUINT32 i = 0; i < count; i++)
        {
            rt_script_clear(items[i]);
            ccalloc(items[i], 0, NULL);
        }
        if (scripts.array)
            ccalloc(scripts.array, 0, NULL);
    }
    mk_value_clear(&value);
    (void)have_value;
    return ok;
}

/* ============================================================================
 * 模块配置
 * ========================================================================== */

CCBOOL mk_runtime_init_globals(MKRuntime *runtime)
{
    if (!runtime || !runtime->semantic)
        return CCFALSE;
    if (runtime->globals_initialized)
        return runtime->failed ? CCFALSE : CCTRUE;
    if (!runtime->semantic->analyzed || !runtime->semantic->ok)
        return rt_error(
            runtime, mk_ast_root(runtime->semantic->ast),
            "语义分析未通过"
        );

    runtime->module_env = env_create(NULL);
    if (!runtime->module_env)
        return rt_error(
            runtime, mk_ast_root(runtime->semantic->ast),
            "内存分配失败"
        );

    MKASTNodeId root = mk_ast_root(runtime->semantic->ast);
    const MKASTNode *root_node =
        mk_ast_node(runtime->semantic->ast, root);
    if (!root_node)
        return rt_error(runtime, root, "模块根节点缺失");

    for (MKASTNodeId child = root_node->first_child;
         child != MK_AST_NODE_NIL;)
    {
        const MKASTNode *child_node =
            mk_ast_node(runtime->semantic->ast, child);
        if (!child_node)
            return rt_error(runtime, child, "模块节点缺失");
        MKASTNodeId next = child_node->next_sibling;
        if (child_node->kind == MK_AST_VAR_DECL)
        {
            if (!rt_eval_var_decl(runtime, child, runtime->module_env))
                return CCFALSE;
        }
        child = next;
    }

    runtime->globals_initialized = CCTRUE;
    return CCTRUE;
}

static CCBOOL mk_runtime_call_function_inner(
    MKRuntime *runtime,
    const MKFunctionInfo *function,
    const MKValue *args, CCUINT32 argc,
    MKValue *out
)
{
    if (function->module != runtime->semantic)
    {
        MKRuntime *target = runtime->module_resolver
            ? runtime->module_resolver(runtime, function->module)
            : NULL;
        if (!target || target == runtime)
            return rt_error(
                runtime, function->decl,
                "跨模块函数调用缺少运行时：%s",
                function->name.data
            );
        if (!mk_runtime_init_globals(target))
            return CCFALSE;
        return mk_runtime_call_function(
            target, function, args, argc, out
        );
    }
    return rt_script_execute(runtime, function, args, argc, out);
}

static CCBOOL mk_runtime_configure_body(MKRuntime *runtime)
{
    if (!runtime || !runtime->semantic)
        return CCFALSE;
    if (runtime->configured)
        return runtime->failed ? CCFALSE : CCTRUE;

    runtime->configure_start = clock();
    runtime->timer_started =
        (runtime->configure_start == (clock_t)-1) ? CCFALSE : CCTRUE;

    if (!mk_runtime_init_globals(runtime))
    {
        runtime->configured = CCTRUE;
        return CCFALSE;
    }
    runtime->configured = CCTRUE;

    return CCTRUE;
}

typedef struct mk_configure_stack_ctx {
    MKRuntime *runtime;
} MKConfigureStackCtx;

static CCBOOL mk_configure_stack_entry(void *user)
{
    MKConfigureStackCtx *ctx = (MKConfigureStackCtx *)user;
    return mk_runtime_configure_body(ctx->runtime);
}

CCBOOL mk_runtime_configure(MKRuntime *runtime)
{
    if (!runtime || !runtime->semantic)
        return CCFALSE;
    if (runtime->configured)
        return runtime->failed ? CCFALSE : CCTRUE;

    MKConfigureStackCtx ctx;
    ctx.runtime = runtime;
    CCBOOL result = CCFALSE;
    if (!mk_exec_stack_run(
            mk_configure_stack_entry, &ctx, &result
        ))
        return rt_error(
            runtime, MK_AST_NODE_NIL, "执行栈分配失败"
        );
    return result;
}

void mk_runtime_set_module_resolver(
    MKRuntime *runtime,
    MKRuntimeModuleResolver resolver, void *user
)
{
    if (!runtime)
        return;
    runtime->module_resolver = resolver;
    runtime->resolver_user = user;
}

void *mk_runtime_get_resolver_user(const MKRuntime *runtime)
{
    return runtime ? runtime->resolver_user : NULL;
}

void mk_runtime_set_work_dir(MKRuntime *runtime, const char *work_dir)
{
    if (runtime)
        runtime->work_dir = work_dir;
}

const char *mk_runtime_get_work_dir(const MKRuntime *runtime)
{
    return runtime ? runtime->work_dir : NULL;
}

void mk_runtime_set_policy(
    MKRuntime *runtime, const MKExecutionPolicy *policy
)
{
    if (!runtime || !policy)
        return;
    runtime->policy = *policy;
}

const MKExecutionPolicy *mk_runtime_get_policy(
    const MKRuntime *runtime
)
{
    return runtime ? &runtime->policy : NULL;
}

static CCBOOL rt_step(MKRuntime *runtime, MKASTNodeId node)
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

CCBOOL mk_runtime_is_configured(const MKRuntime *runtime)
{
    return (runtime && runtime->configured) ? CCTRUE : CCFALSE;
}

CCBOOL mk_runtime_get_global(
    const MKRuntime *runtime,
    const char *name, CCUINT32 name_len,
    MKValue *out
)
{
    if (!runtime || !runtime->module_env || !name || !out)
        return CCFALSE;
    MKEnvVar *slot = env_slot(runtime->module_env, name, name_len);
    if (!slot)
        return CCFALSE;
    return mk_value_copy(out, &slot->value);
}

/* ============================================================================
 * 生命周期
 * ========================================================================== */

MKRuntime *mk_runtime_create(
    MKSemanticModule *semantic,
    MKConfig *config,
    MKBuiltinRegistry *builtins,
    MKDiagnostic *diagnostic
)
{
    if (!semantic || !config || !builtins || !diagnostic)
        return NULL;

    MKRuntime *runtime =
        ccalloc(NULL, sizeof(MKRuntime), MK_RT_COMMENT);
    if (!runtime)
        return NULL;
    memset(runtime, 0, sizeof(*runtime));
    runtime->semantic = semantic;
    runtime->config = config;
    runtime->builtins = builtins;
    runtime->diagnostic = diagnostic;
    return runtime;
}

void mk_runtime_destroy(MKRuntime *runtime)
{
    if (!runtime)
        return;
    if (runtime->module_env)
        env_destroy(runtime->module_env);
    ccalloc(runtime, 0, NULL);
}
