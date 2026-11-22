/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 冻结计划、Lowering 与验证（M5）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_ir.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <string.h>

#define CMK_IR_COMMENT CC_DBG_REL_TAG(".cmk 计划", NULL)

struct cmk_plan {
    CCDyn targets; /* CMKPlanTarget[] */
    CCDyn order;   /* CMKTargetId[] */
    CMKTargetId root;
    CCBOOL frozen;
};

static CCBOOL cmk_plan_error(
    CMKDiagnostic *diagnostic,
    const char *module, CCUINT32 line,
    const char *fmt, ...
)
{
    if (!diagnostic)
        return CCFALSE;
    va_list ap;
    va_start(ap, fmt);
    CCBOOL ok = cmk_diagnostic_vset(
        diagnostic, CMK_DIAG_ERROR, module, line, 0, fmt, ap
    );
    va_end(ap);
    return ok ? CCFALSE : CCFALSE;
}

CMKPlan *cmk_plan_create(void)
{
    CMKPlan *plan = ccalloc(NULL, sizeof(CMKPlan), CMK_IR_COMMENT);
    if (!plan)
        return NULL;
    memset(plan, 0, sizeof(*plan));
    plan->root = CMK_TARGET_ID_NIL;
    return plan;
}

void cmk_plan_destroy(CMKPlan *plan)
{
    if (!plan)
        return;
    CMKPlanTarget *targets = (CMKPlanTarget *)plan->targets.array;
    CCUINT32 count =
        plan->targets.size / (CCUINT32)sizeof(CMKPlanTarget);
    if (plan->order.array)
        ccalloc(plan->order.array, 0, NULL);
    for (CCUINT32 i = 0; i < count; i++)
        cmk_plan_target_clear(&targets[i]);
    if (plan->targets.array)
        ccalloc(plan->targets.array, 0, NULL);
    ccalloc(plan, 0, NULL);
}

static const CMKList *cmk_value_list(
    const CMKValue *value, CMKValueType element
)
{
    if (!value || value->type != CMK_VALUE_LIST ||
        !value->as.list || value->as.list->element_type != element)
        return NULL;
    return value->as.list;
}

CCBOOL cmk_plan_add_draft(
    CMKPlan *plan,
    const char *module_path, CCUINT32 module_len,
    const CMKTargetDraft *draft,
    CMKDiagnostic *diagnostic
)
{
    if (!plan || plan->frozen || !draft || !draft->info ||
        (!module_path && module_len))
        return CCFALSE;

    CMKPlanTarget target;
    cmk_plan_target_init(&target);
    if (!cmk_target_key_set(
            &target.key, module_path, module_len,
            draft->info->name.data, draft->info->name.len
        ))
    {
        cmk_plan_target_clear(&target);
        return cmk_plan_error(
            diagnostic, module_path, 0, "计划目标名复制失败"
        );
    }

    const CMKList *inputs =
        cmk_value_list(&draft->inputs, CMK_VALUE_STRING);
    const CMKList *outputs =
        cmk_value_list(&draft->outputs, CMK_VALUE_STRING);
    const CMKList *actions =
        cmk_value_list(&draft->actions, CMK_VALUE_ACTION);
    const CMKList *dependencies =
        cmk_value_list(&draft->depends_on, CMK_VALUE_TARGET_REF);
    if (!inputs || !outputs || !actions || !dependencies)
    {
        cmk_plan_target_clear(&target);
        return cmk_plan_error(
            diagnostic, module_path, 0,
            "target 字段类型非法：%s", draft->info->name.data
        );
    }

    CCUINT32 input_count = cmk_list_count(inputs);
    for (CCUINT32 i = 0; i < input_count; i++)
    {
        const CMKValue *value = cmk_list_at(inputs, i);
        if (!value || value->type != CMK_VALUE_STRING ||
            !cmk_plan_target_add_input(
                &target, value->as.string.data, value->as.string.len
            ))
        {
            cmk_plan_target_clear(&target);
            return cmk_plan_error(
                diagnostic, module_path, 0, "计划输入复制失败"
            );
        }
    }

    CCUINT32 output_count = cmk_list_count(outputs);
    for (CCUINT32 i = 0; i < output_count; i++)
    {
        const CMKValue *value = cmk_list_at(outputs, i);
        if (!value || value->type != CMK_VALUE_STRING ||
            !cmk_plan_target_add_output(
                &target, value->as.string.data, value->as.string.len
            ))
        {
            cmk_plan_target_clear(&target);
            return cmk_plan_error(
                diagnostic, module_path, 0, "计划输出复制失败"
            );
        }
    }

    CCUINT32 action_count = cmk_list_count(actions);
    for (CCUINT32 i = 0; i < action_count; i++)
    {
        const CMKValue *value = cmk_list_at(actions, i);
        if (!value || value->type != CMK_VALUE_ACTION ||
            !value->as.action)
        {
            cmk_plan_target_clear(&target);
            return cmk_plan_error(
                diagnostic, module_path, 0, "动作值非法"
            );
        }
        CMKPlanAction action;
        if (!cmk_plan_action_copy(&action, value->as.action) ||
            !cmk_plan_target_add_action(&target, &action))
        {
            cmk_plan_action_clear(&action);
            cmk_plan_target_clear(&target);
            return cmk_plan_error(
                diagnostic, module_path, 0, "动作复制失败"
            );
        }
        cmk_plan_action_clear(&action);
    }

    CCUINT32 dependency_count = cmk_list_count(dependencies);
    for (CCUINT32 i = 0; i < dependency_count; i++)
    {
        const CMKValue *value = cmk_list_at(dependencies, i);
        if (!value || value->type != CMK_VALUE_TARGET_REF ||
            !cmk_plan_target_add_dependency_key(
                &target,
                value->as.target_ref.module.data,
                value->as.target_ref.module.len,
                value->as.target_ref.name.data,
                value->as.target_ref.name.len
            ))
        {
            cmk_plan_target_clear(&target);
            return cmk_plan_error(
                diagnostic, module_path, 0, "目标依赖复制失败"
            );
        }
    }

    if (!cmk_dyn_push(
            &plan->targets, (CCUINT32)sizeof(CMKPlanTarget),
            (CCINT32)sizeof(CMKPlanTarget), &target
        ))
    {
        cmk_plan_target_clear(&target);
        return cmk_plan_error(
            diagnostic, module_path, 0, "计划目标追加失败"
        );
    }
    return CCTRUE;
}

CCBOOL cmk_plan_is_frozen(const CMKPlan *plan)
{
    return plan ? plan->frozen : CCFALSE;
}

CCUINT32 cmk_plan_target_count(const CMKPlan *plan)
{
    return plan
        ? plan->targets.size / (CCUINT32)sizeof(CMKPlanTarget)
        : 0;
}

const CMKPlanTarget *cmk_plan_target_at(
    const CMKPlan *plan, CMKTargetId id
)
{
    if (!plan || id >= cmk_plan_target_count(plan))
        return NULL;
    return &((const CMKPlanTarget *)plan->targets.array)[id];
}

CMKTargetId cmk_plan_find_target(
    const CMKPlan *plan,
    const char *module_path, CCUINT32 module_len,
    const char *name, CCUINT32 name_len
)
{
    if (!plan)
        return CMK_TARGET_ID_NIL;
    CMKTargetKey key;
    cmk_target_key_init(&key);
    CCBOOL have_key = cmk_target_key_set(
        &key, module_path, module_len, name, name_len
    );
    CCUINT32 count = cmk_plan_target_count(plan);
    if (have_key)
    {
        for (CCUINT32 i = 0; i < count; i++)
        {
            const CMKPlanTarget *target = cmk_plan_target_at(plan, i);
            if (cmk_target_key_equal(&target->key, &key))
            {
                cmk_target_key_clear(&key);
                return i;
            }
        }
    }
    /* 兼容旧路径写法：模块路径不一致时，若目标名在计划中唯一则接受 */
    CMKTargetId found = CMK_TARGET_ID_NIL;
    CCUINT32 matches = 0;
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKPlanTarget *target = cmk_plan_target_at(plan, i);
        if (target->key.name.len == name_len &&
            (name_len == 0 ||
             memcmp(target->key.name.data, name, name_len) == 0))
        {
            matches++;
            found = i;
        }
    }
    if (have_key)
        cmk_target_key_clear(&key);
    return matches == 1 ? found : CMK_TARGET_ID_NIL;
}

static CMKTargetId cmk_plan_find_name(
    const CMKPlan *plan, const char *name, CCUINT32 name_len,
    CCUINT32 *matches
)
{
    *matches = 0;
    CMKTargetId found = CMK_TARGET_ID_NIL;
    CCUINT32 count = cmk_plan_target_count(plan);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKPlanTarget *target = cmk_plan_target_at(plan, i);
        if (target->key.name.len == name_len &&
            (name_len == 0 ||
             memcmp(target->key.name.data, name, name_len) == 0))
        {
            (*matches)++;
            found = i;
        }
    }
    return *matches == 1 ? found : CMK_TARGET_ID_NIL;
}

static CCBOOL cmk_plan_validate_action(
    CMKDiagnostic *diagnostic,
    const CMKPlanTarget *target,
    const CMKPlanAction *action
)
{
    const char *missing = NULL;
    switch (action->type)
    {
    case CMK_PLAN_ACTION_EXEC:
        if (action->program.len == 0)
            missing = "exec 缺少 program";
        break;
    case CMK_PLAN_ACTION_COPY:
    case CMK_PLAN_ACTION_MOVE:
        if (action->from.len == 0 || action->to.len == 0)
            missing = "文件动作缺少 from/to";
        break;
    case CMK_PLAN_ACTION_REMOVE:
    case CMK_PLAN_ACTION_MKDIR:
        if (action->path.len == 0)
            missing = "路径动作缺少 path";
        break;
    default:
        missing = "未知动作类型";
        break;
    }
    if (!missing)
        return CCTRUE;
    return cmk_plan_error(
        diagnostic, target->key.module.data, 0,
        "target %s 的动作非法：%s",
        target->key.name.data, missing
    );
}

static CCBOOL cmk_plan_validate(
    CMKPlan *plan, CMKDiagnostic *diagnostic
)
{
    CCUINT32 count = cmk_plan_target_count(plan);
    CMKPlanTarget *targets = (CMKPlanTarget *)plan->targets.array;

    for (CCUINT32 i = 0; i < count; i++)
    {
        for (CCUINT32 j = i + 1; j < count; j++)
        {
            if (cmk_target_key_equal(
                    &targets[i].key, &targets[j].key
                ))
                return cmk_plan_error(
                    diagnostic, targets[i].key.module.data, 0,
                    "目标名重复：%s", targets[i].key.name.data
                );
        }
    }

    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKPlanTarget *target = &targets[i];
        CMKTargetKey *keys =
            (CMKTargetKey *)target->dependency_keys.array;
        CCUINT32 key_count = target->dependency_keys.size /
            (CCUINT32)sizeof(CMKTargetKey);
        for (CCUINT32 k = 0; k < key_count; k++)
        {
            CMKTargetId dep = cmk_plan_find_target(
                plan, keys[k].module.data, keys[k].module.len,
                keys[k].name.data, keys[k].name.len
            );
            if (dep == CMK_TARGET_ID_NIL)
            {
                CCUINT32 matches = 0;
                dep = cmk_plan_find_name(
                    plan, keys[k].name.data, keys[k].name.len, &matches
                );
            }
            if (dep == CMK_TARGET_ID_NIL)
                return cmk_plan_error(
                    diagnostic, target->key.module.data, 0,
                    "依赖目标不存在：%s.%s",
                    keys[k].module.data, keys[k].name.data
                );
            if (!cmk_plan_target_add_dependency(target, dep))
                return cmk_plan_error(
                    diagnostic, target->key.module.data, 0,
                    "依赖写入失败"
                );
        }
        for (CCUINT32 k = 0; k < key_count; k++)
            cmk_target_key_clear(&keys[k]);
        if (target->dependency_keys.array)
            ccalloc(target->dependency_keys.array, 0, NULL);
        memset(&target->dependency_keys, 0, sizeof(target->dependency_keys));

        if (target->actions.size == 0 &&
            target->dependencies.size == 0 &&
            target->inputs.size == 0 &&
            target->outputs.size == 0)
            return cmk_plan_error(
                diagnostic, target->key.module.data, 0,
                "空目标既无动作也无依赖：%s", target->key.name.data
            );

        CMKPlanAction *actions =
            (CMKPlanAction *)target->actions.array;
        CCUINT32 action_count = target->actions.size /
            (CCUINT32)sizeof(CMKPlanAction);
        for (CCUINT32 a = 0; a < action_count; a++)
        {
            if (!cmk_plan_validate_action(
                    diagnostic, target, &actions[a]
                ))
                return CCFALSE;
        }
    }

    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKString *left = (CMKString *)targets[i].outputs.array;
        CCUINT32 left_count = targets[i].outputs.size /
            (CCUINT32)sizeof(CMKString);
        for (CCUINT32 a = 0; a < left_count; a++)
        {
            for (CCUINT32 j = i; j < count; j++)
            {
                CMKString *right = (CMKString *)targets[j].outputs.array;
                CCUINT32 right_count = targets[j].outputs.size /
                    (CCUINT32)sizeof(CMKString);
                CCUINT32 start = (i == j) ? a + 1 : 0;
                for (CCUINT32 b = start; b < right_count; b++)
                {
                    if (cmk_string_equal(&left[a], &right[b]))
                        return cmk_plan_error(
                            diagnostic, targets[i].key.module.data, 0,
                            "输出文件重复：%s", left[a].data
                        );
                }
            }
        }
    }
    return CCTRUE;
}

static CCBOOL cmk_plan_topology(
    CMKPlan *plan, CMKDiagnostic *diagnostic
)
{
    CCUINT32 count = cmk_plan_target_count(plan);
    CMKPlanTarget *targets = (CMKPlanTarget *)plan->targets.array;
    CCUINT32 *indegree = ccalloc(
        NULL, (CCUINT64)count * sizeof(CCUINT32), CMK_IR_COMMENT
    );
    CCBOOL *placed = ccalloc(
        NULL, (CCUINT64)count * sizeof(CCBOOL), CMK_IR_COMMENT
    );
    if ((count > 0 && !indegree) || (count > 0 && !placed))
    {
        if (indegree)
            ccalloc(indegree, 0, NULL);
        if (placed)
            ccalloc(placed, 0, NULL);
        return cmk_plan_error(
            diagnostic, NULL, 0, "拓扑排序内存分配失败"
        );
    }
    if (count > 0)
    {
        memset(indegree, 0, (size_t)count * sizeof(CCUINT32));
        memset(placed, 0, (size_t)count * sizeof(CCBOOL));
    }

    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKTargetId *deps =
            (CMKTargetId *)targets[i].dependencies.array;
        CCUINT32 dep_count = targets[i].dependencies.size /
            (CCUINT32)sizeof(CMKTargetId);
        for (CCUINT32 d = 0; d < dep_count; d++)
        {
            if (deps[d] >= count)
            {
                if (indegree)
                    ccalloc(indegree, 0, NULL);
                if (placed)
                    ccalloc(placed, 0, NULL);
                return cmk_plan_error(
                    diagnostic, targets[i].key.module.data, 0,
                    "依赖 ID 越界"
                );
            }
        }
        indegree[i] = dep_count;
    }

    for (CCUINT32 emitted = 0; emitted < count; emitted++)
    {
        CMKTargetId next = CMK_TARGET_ID_NIL;
        for (CCUINT32 i = 0; i < count; i++)
        {
            if (!placed[i] && indegree[i] == 0)
            {
                next = i;
                break;
            }
        }
        if (next == CMK_TARGET_ID_NIL)
        {
            if (indegree)
                ccalloc(indegree, 0, NULL);
            if (placed)
                ccalloc(placed, 0, NULL);
            return cmk_plan_error(
                diagnostic, NULL, 0, "目标依赖存在环"
            );
        }
        placed[next] = CCTRUE;
        if (!cmk_dyn_push(
                &plan->order, (CCUINT32)sizeof(CMKTargetId),
                (CCINT32)sizeof(CMKTargetId), &next
            ))
        {
            if (indegree)
                ccalloc(indegree, 0, NULL);
            if (placed)
                ccalloc(placed, 0, NULL);
            return cmk_plan_error(
                diagnostic, NULL, 0, "拓扑序写入失败"
            );
        }
        for (CCUINT32 j = 0; j < count; j++)
        {
            if (placed[j])
                continue;
            CMKTargetId *deps =
                (CMKTargetId *)targets[j].dependencies.array;
            CCUINT32 dep_count = targets[j].dependencies.size /
                (CCUINT32)sizeof(CMKTargetId);
            for (CCUINT32 d = 0; d < dep_count; d++)
            {
                if (deps[d] == next)
                {
                    indegree[j]--;
                    break;
                }
            }
        }
    }

    if (indegree)
        ccalloc(indegree, 0, NULL);
    if (placed)
        ccalloc(placed, 0, NULL);
    return CCTRUE;
}

CCBOOL cmk_plan_freeze(
    CMKPlan *plan,
    const char *root_module, CCUINT32 root_module_len,
    const char *root_name, CCUINT32 root_name_len,
    CMKDiagnostic *diagnostic
)
{
    if (!plan || plan->frozen)
        return CCFALSE;
    if (!cmk_plan_validate(plan, diagnostic))
        return CCFALSE;

    CMKTargetId root = cmk_plan_find_target(
        plan, root_module, root_module_len, root_name, root_name_len
    );
    if (root == CMK_TARGET_ID_NIL)
    {
        CCUINT32 matches = 0;
        root = cmk_plan_find_name(
            plan, root_name, root_name_len, &matches
        );
    }
    if (root == CMK_TARGET_ID_NIL)
        return cmk_plan_error(
            diagnostic, root_module, 0,
            "根目标不存在：%s", root_name ? root_name : "main"
        );

    if (!cmk_plan_topology(plan, diagnostic))
        return CCFALSE;
    plan->root = root;
    plan->frozen = CCTRUE;
    return CCTRUE;
}

CMKTargetId cmk_plan_root(const CMKPlan *plan)
{
    return plan ? plan->root : CMK_TARGET_ID_NIL;
}

CCUINT32 cmk_plan_order_count(const CMKPlan *plan)
{
    return plan ? plan->order.size / (CCUINT32)sizeof(CMKTargetId) : 0;
}

CMKTargetId cmk_plan_order_at(const CMKPlan *plan, CCUINT32 index)
{
    if (!plan || index >= cmk_plan_order_count(plan))
        return CMK_TARGET_ID_NIL;
    return ((const CMKTargetId *)plan->order.array)[index];
}

void cmk_plan_set_target_state(
    CMKPlan *plan, CMKTargetId id, CMKTargetState state
)
{
    if (!plan || id >= cmk_plan_target_count(plan))
        return;
    ((CMKPlanTarget *)plan->targets.array)[id].state = state;
}
