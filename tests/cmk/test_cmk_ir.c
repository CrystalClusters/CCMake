/*
 * @Description: CMK 构建计划 IR 与验证测试（M5）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_ir.h"
#include "cmk/cmk_semantic_internal.h"
#include "cmk/cmk_target_draft.h"
#include "cmk/cmk_target_internal.h"

#include <string.h>

static void ir_info_init(CMKTargetInfo *info, const char *name)
{
    memset(info, 0, sizeof(*info));
    cmk_string_set(&info->name, name, (CCUINT32)strlen(name));
}

static void ir_draft_init(CMKTargetDraft *draft, CMKTargetInfo *info)
{
    memset(draft, 0, sizeof(*draft));
    draft->info = info;
    cmk_value_set_list(&draft->depends_on, CMK_VALUE_TARGET_REF);
    cmk_value_set_list(&draft->inputs, CMK_VALUE_STRING);
    cmk_value_set_list(&draft->outputs, CMK_VALUE_STRING);
    cmk_value_set_list(&draft->actions, CMK_VALUE_ACTION);
}

static void ir_draft_clear(CMKTargetDraft *draft)
{
    cmk_value_clear(&draft->depends_on);
    cmk_value_clear(&draft->inputs);
    cmk_value_clear(&draft->outputs);
    cmk_value_clear(&draft->actions);
}

static void ir_add_dep(
    CMKTargetDraft *draft, const char *module, const char *name
)
{
    CMKValue value;
    cmk_value_init(&value);
    cmk_value_set_target_ref(
        &value, module, (CCUINT32)strlen(module),
        name, (CCUINT32)strlen(name)
    );
    cmk_list_append(draft->depends_on.as.list, &value);
    cmk_value_clear(&value);
}

static void ir_add_output(CMKTargetDraft *draft, const char *path)
{
    CMKValue value;
    cmk_value_init(&value);
    cmk_value_set_string(
        &value, path, (CCUINT32)strlen(path)
    );
    cmk_list_append(draft->outputs.as.list, &value);
    cmk_value_clear(&value);
}

static void ir_add_exec(CMKTargetDraft *draft, const char *program)
{
    CMKValue value;
    cmk_value_init(&value);
    cmk_value_set_action(&value, CMK_ACTION_EXEC);
    cmk_string_set(
        &value.as.action->program, program,
        (CCUINT32)strlen(program)
    );
    cmk_list_append(draft->actions.as.list, &value);
    cmk_value_clear(&value);
}

static void ir_add_mkdir(CMKTargetDraft *draft, const char *path)
{
    CMKValue value;
    cmk_value_init(&value);
    cmk_value_set_action(&value, CMK_ACTION_MKDIR);
    cmk_string_set(
        &value.as.action->path, path,
        (CCUINT32)strlen(path)
    );
    cmk_list_append(draft->actions.as.list, &value);
    cmk_value_clear(&value);
}

static void ir_case_001(void)
{
    CC_CASE("IR-001", "目标、动作与拓扑序");
    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKPlan *plan = cmk_plan_create();
    CC_CHECK(plan != NULL, "计划创建");

    CMKTargetInfo info_a;
    CMKTargetInfo info_b;
    ir_info_init(&info_a, "A");
    ir_info_init(&info_b, "B");
    CMKTargetDraft a;
    CMKTargetDraft b;
    ir_draft_init(&a, &info_a);
    ir_draft_init(&b, &info_b);
    ir_add_dep(&a, "mod", "B");
    ir_add_exec(&a, "cc");
    ir_add_output(&a, "a.out");
    ir_add_mkdir(&b, "out");
    ir_add_output(&b, "out");

    CC_CHECK(
        cmk_plan_add_draft(plan, "mod", 3, &a, &diagnostic),
        "追加 A"
    );
    CC_CHECK(
        cmk_plan_add_draft(plan, "mod", 3, &b, &diagnostic),
        "追加 B"
    );
    CC_CHECK(
        cmk_plan_freeze(plan, "mod", 3, "A", 1, &diagnostic),
        "冻结计划"
    );
    CC_CHECK_EQ_I64(
        "目标数", 2, (CCINT64)cmk_plan_target_count(plan)
    );
    CC_CHECK_EQ_I64(
        "根目标", 0, (CCINT64)cmk_plan_root(plan)
    );
    CC_CHECK_EQ_I64(
        "拓扑序 B", 1, (CCINT64)cmk_plan_order_at(plan, 0)
    );
    CC_CHECK_EQ_I64(
        "拓扑序 A", 0, (CCINT64)cmk_plan_order_at(plan, 1)
    );
    const CMKPlanTarget *target_a = cmk_plan_target_at(plan, 0);
    CC_CHECK(target_a && target_a->actions.size >=
             (CCUINT32)sizeof(CMKPlanAction), "A 动作复制");
    if (target_a && target_a->actions.size >=
        (CCUINT32)sizeof(CMKPlanAction))
    {
        const CMKPlanAction *action =
            (const CMKPlanAction *)target_a->actions.array;
        CC_CHECK_EQ_STR("动作程序", "cc", action->program.data);
    }

    ir_draft_clear(&a);
    ir_draft_clear(&b);
    cmk_string_clear(&info_a.name);
    cmk_string_clear(&info_b.name);
    cmk_plan_destroy(plan);
    cmk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

static void ir_case_002(void)
{
    CC_CASE("IR-002", "计划验证错误");
    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CMKPlan *plan = cmk_plan_create();
    CMKTargetInfo info_a;
    ir_info_init(&info_a, "A");
    CMKTargetDraft a;
    ir_draft_init(&a, &info_a);
    ir_add_dep(&a, "mod", "missing");
    ir_add_exec(&a, "cc");
    CC_CHECK(
        cmk_plan_add_draft(plan, "mod", 3, &a, &diagnostic),
        "追加缺失依赖目标"
    );
    CC_CHECK(
        !cmk_plan_freeze(plan, "mod", 3, "A", 1, &diagnostic),
        "缺失依赖拒绝"
    );
    CC_CHECK(
        diagnostic.message &&
        strstr(diagnostic.message, "依赖") != NULL,
        "缺失依赖诊断"
    );
    ir_draft_clear(&a);
    cmk_string_clear(&info_a.name);
    cmk_plan_destroy(plan);
    cmk_diagnostic_clear(&diagnostic);
    CC_CASE_END();
}

void test_cmk_ir(void)
{
    ir_case_001();
    ir_case_002();
}
