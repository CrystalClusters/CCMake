/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 冻结构建计划中的目标与动作 IR（M5）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_target.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <string.h>

#define CMK_TARGET_COMMENT CC_DBG_REL_TAG(".cmk 计划", NULL)

void cmk_target_key_init(CMKTargetKey *key)
{
    if (!key)
        return;
    cmk_string_init(&key->module);
    cmk_string_init(&key->name);
}

void cmk_target_key_clear(CMKTargetKey *key)
{
    if (!key)
        return;
    cmk_string_clear(&key->module);
    cmk_string_clear(&key->name);
}

CCBOOL cmk_target_key_set(
    CMKTargetKey *key,
    const char *module, CCUINT32 module_len,
    const char *name, CCUINT32 name_len
)
{
    if (!key || (!module && module_len) || (!name && name_len))
        return CCFALSE;
    while (module_len > 1 &&
           (module[module_len - 1] == '/' ||
            module[module_len - 1] == '\\'))
        module_len--;
    if (!cmk_string_set(&key->module, module, module_len))
        return CCFALSE;
    if (!cmk_string_set(&key->name, name, name_len))
        return CCFALSE;
    return CCTRUE;
}

CCBOOL cmk_target_key_equal(
    const CMKTargetKey *a, const CMKTargetKey *b
)
{
    return (a && b &&
            cmk_string_equal(&a->module, &b->module) &&
            cmk_string_equal(&a->name, &b->name))
        ? CCTRUE : CCFALSE;
}

void cmk_plan_action_init(CMKPlanAction *action)
{
    if (!action)
        return;
    memset(action, 0, sizeof(*action));
    cmk_string_init(&action->program);
    cmk_string_init(&action->cwd);
    cmk_string_init(&action->from);
    cmk_string_init(&action->to);
    cmk_string_init(&action->path);
    memset(&action->argv, 0, sizeof(action->argv));
    memset(&action->env, 0, sizeof(action->env));
}

static void cmk_string_array_clear(CCDyn *array)
{
    if (!array)
        return;
    CMKString *items = (CMKString *)array->array;
    CCUINT32 count = array->size / (CCUINT32)sizeof(CMKString);
    for (CCUINT32 i = 0; i < count; i++)
        cmk_string_clear(&items[i]);
    if (array->array)
        ccalloc(array->array, 0, NULL);
    memset(array, 0, sizeof(*array));
}

void cmk_plan_action_clear(CMKPlanAction *action)
{
    if (!action)
        return;
    cmk_string_clear(&action->program);
    cmk_string_clear(&action->cwd);
    cmk_string_clear(&action->from);
    cmk_string_clear(&action->to);
    cmk_string_clear(&action->path);
    cmk_string_array_clear(&action->argv);
    cmk_string_array_clear(&action->env);
    memset(action, 0, sizeof(*action));
}

static CCBOOL cmk_string_array_push(
    CCDyn *array, const char *data, CCUINT32 len
)
{
    CMKString string;
    cmk_string_init(&string);
    if (!cmk_string_set(&string, data, len))
        return CCFALSE;
    if (!cmk_dyn_push(
            array, (CCUINT32)sizeof(CMKString),
            (CCINT32)sizeof(CMKString), &string
        ))
    {
        cmk_string_clear(&string);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL cmk_plan_action_append_arg(
    CMKPlanAction *action, const char *data, CCUINT32 len
)
{
    if (!action)
        return CCFALSE;
    if (!cmk_string_array_push(&action->argv, data, len))
        return CCFALSE;
    action->argc++;
    return CCTRUE;
}

CCBOOL cmk_plan_action_append_env(
    CMKPlanAction *action, const char *data, CCUINT32 len
)
{
    if (!action)
        return CCFALSE;
    if (!cmk_string_array_push(&action->env, data, len))
        return CCFALSE;
    action->envc++;
    return CCTRUE;
}

static CCBOOL cmk_string_list_copy(
    CCDyn *dst, const CMKList *src
)
{
    if (!src)
        return CCTRUE;
    CCUINT32 count = cmk_list_count(src);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKValue *value = cmk_list_at(src, i);
        if (!value || value->type != CMK_VALUE_STRING)
            return CCFALSE;
        if (!cmk_string_array_push(
                dst, value->as.string.data, value->as.string.len
            ))
            return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL cmk_plan_action_copy(
    CMKPlanAction *dst, const CMKAction *src
)
{
    if (!dst || !src)
        return CCFALSE;
    cmk_plan_action_init(dst);
    switch (src->kind)
    {
    case CMK_ACTION_EXEC: dst->type = CMK_PLAN_ACTION_EXEC; break;
    case CMK_ACTION_COPY: dst->type = CMK_PLAN_ACTION_COPY; break;
    case CMK_ACTION_MOVE: dst->type = CMK_PLAN_ACTION_MOVE; break;
    case CMK_ACTION_REMOVE: dst->type = CMK_PLAN_ACTION_REMOVE; break;
    case CMK_ACTION_MKDIR: dst->type = CMK_PLAN_ACTION_MKDIR; break;
    default:
        cmk_plan_action_clear(dst);
        return CCFALSE;
    }
    if (!cmk_string_set(
            &dst->program, src->program.data, src->program.len
        ) ||
        !cmk_string_set(&dst->cwd, src->cwd.data, src->cwd.len) ||
        !cmk_string_set(&dst->from, src->from.data, src->from.len) ||
        !cmk_string_set(&dst->to, src->to.data, src->to.len) ||
        !cmk_string_set(&dst->path, src->path.data, src->path.len) ||
        !cmk_string_list_copy(&dst->argv, src->args) ||
        !cmk_string_list_copy(&dst->env, src->env))
    {
        cmk_plan_action_clear(dst);
        return CCFALSE;
    }
    dst->argc = src->args ? cmk_list_count(src->args) : 0;
    dst->envc = src->env ? cmk_list_count(src->env) : 0;
    return CCTRUE;
}

void cmk_plan_target_init(CMKPlanTarget *target)
{
    if (!target)
        return;
    memset(target, 0, sizeof(*target));
    cmk_target_key_init(&target->key);
    target->state = CMK_TARGET_PENDING;
}

void cmk_plan_target_clear(CMKPlanTarget *target)
{
    if (!target)
        return;
    cmk_target_key_clear(&target->key);
    CMKPlanAction *actions = (CMKPlanAction *)target->actions.array;
    CCUINT32 action_count =
        target->actions.size / (CCUINT32)sizeof(CMKPlanAction);
    for (CCUINT32 i = 0; i < action_count; i++)
        cmk_plan_action_clear(&actions[i]);
    if (target->actions.array)
        ccalloc(target->actions.array, 0, NULL);
    cmk_string_array_clear(&target->inputs);
    cmk_string_array_clear(&target->outputs);
    CMKTargetKey *keys = (CMKTargetKey *)target->dependency_keys.array;
    CCUINT32 key_count =
        target->dependency_keys.size / (CCUINT32)sizeof(CMKTargetKey);
    for (CCUINT32 i = 0; i < key_count; i++)
        cmk_target_key_clear(&keys[i]);
    if (target->dependency_keys.array)
        ccalloc(target->dependency_keys.array, 0, NULL);
    if (target->dependencies.array)
        ccalloc(target->dependencies.array, 0, NULL);
    memset(target, 0, sizeof(*target));
}

CCBOOL cmk_plan_target_add_dependency(
    CMKPlanTarget *target, CMKTargetId dependency
)
{
    if (!target)
        return CCFALSE;
    return cmk_dyn_push(
        &target->dependencies, (CCUINT32)sizeof(CMKTargetId),
        (CCINT32)sizeof(CMKTargetId), &dependency
    );
}

CCBOOL cmk_plan_target_add_dependency_key(
    CMKPlanTarget *target,
    const char *module, CCUINT32 module_len,
    const char *name, CCUINT32 name_len
)
{
    if (!target)
        return CCFALSE;
    CMKTargetKey key;
    cmk_target_key_init(&key);
    if (!cmk_target_key_set(&key, module, module_len, name, name_len))
        return CCFALSE;
    if (!cmk_dyn_push(
            &target->dependency_keys, (CCUINT32)sizeof(CMKTargetKey),
            (CCINT32)sizeof(CMKTargetKey), &key
        ))
    {
        cmk_target_key_clear(&key);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL cmk_plan_target_add_action(
    CMKPlanTarget *target, const CMKPlanAction *action
)
{
    if (!target || !action)
        return CCFALSE;
    CMKPlanAction copy;
    cmk_plan_action_init(&copy);
    copy.type = action->type;
    if (!cmk_string_set(
            &copy.program, action->program.data, action->program.len
        ) ||
        !cmk_string_set(&copy.cwd, action->cwd.data, action->cwd.len) ||
        !cmk_string_set(&copy.from, action->from.data, action->from.len) ||
        !cmk_string_set(&copy.to, action->to.data, action->to.len) ||
        !cmk_string_set(&copy.path, action->path.data, action->path.len))
    {
        cmk_plan_action_clear(&copy);
        return CCFALSE;
    }
    CMKString *args = (CMKString *)action->argv.array;
    CCUINT32 arg_count = action->argc;
    if (arg_count == 0 && action->argv.size > 0)
        arg_count = action->argv.size / (CCUINT32)sizeof(CMKString);
    for (CCUINT32 i = 0; i < arg_count; i++)
    {
        if (!cmk_plan_action_append_arg(
                &copy, args[i].data, args[i].len
            ))
        {
            cmk_plan_action_clear(&copy);
            return CCFALSE;
        }
    }
    CMKString *env = (CMKString *)action->env.array;
    CCUINT32 env_count = action->envc;
    if (env_count == 0 && action->env.size > 0)
        env_count = action->env.size / (CCUINT32)sizeof(CMKString);
    for (CCUINT32 i = 0; i < env_count; i++)
    {
        if (!cmk_plan_action_append_env(
                &copy, env[i].data, env[i].len
            ))
        {
            cmk_plan_action_clear(&copy);
            return CCFALSE;
        }
    }
    if (!cmk_dyn_push(
            &target->actions, (CCUINT32)sizeof(CMKPlanAction),
            (CCINT32)sizeof(CMKPlanAction), &copy
        ))
    {
        cmk_plan_action_clear(&copy);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL cmk_plan_target_add_input(
    CMKPlanTarget *target, const char *path, CCUINT32 len
)
{
    return target ? cmk_string_array_push(&target->inputs, path, len)
                  : CCFALSE;
}

CCBOOL cmk_plan_target_add_output(
    CMKPlanTarget *target, const char *path, CCUINT32 len
)
{
    return target ? cmk_string_array_push(&target->outputs, path, len)
                  : CCFALSE;
}

const char *cmk_target_state_name(CMKTargetState state)
{
    switch (state)
    {
    case CMK_TARGET_PENDING: return "PENDING";
    case CMK_TARGET_READY: return "READY";
    case CMK_TARGET_RUNNING: return "RUNNING";
    case CMK_TARGET_SUCCESS: return "SUCCESS";
    case CMK_TARGET_FAILED: return "FAILED";
    case CMK_TARGET_BLOCKED: return "BLOCKED";
    case CMK_TARGET_SKIPPED: return "SKIPPED";
    case CMK_TARGET_CANCELLED: return "CANCELLED";
    default: return "UNKNOWN";
    }
}

const char *cmk_plan_action_type_name(CMKPlanActionType type)
{
    switch (type)
    {
    case CMK_PLAN_ACTION_EXEC: return "exec";
    case CMK_PLAN_ACTION_COPY: return "copy";
    case CMK_PLAN_ACTION_MOVE: return "move";
    case CMK_PLAN_ACTION_REMOVE: return "remove";
    case CMK_PLAN_ACTION_MKDIR: return "mkdir";
    default: return "unknown";
    }
}
