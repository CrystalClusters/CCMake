/*
 * @Description: CMK 增量缓存测试（M8）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include "cmk/cmk_cache.h"
#include "cmk/cmk_semantic_internal.h"
#include "cmk/cmk_target_internal.h"

#include <stdio.h>
#include <string.h>

void test_cmk_cache(void)
{
    CC_CASE("CACHE-001", "签名命中与失效");
    remove("out/cmk_cache_test.cache");
    remove("out/cmk_cache_test.cache.tmp");
    remove("out/cmk_cache_in");
    remove("out/cmk_cache_out");
    FILE *file = fopen("out/cmk_cache_in", "wb");
    CC_CHECK(file != NULL, "创建输入");
    if (file)
    {
        fputs("input", file);
        fclose(file);
    }
    file = fopen("out/cmk_cache_out", "wb");
    CC_CHECK(file != NULL, "创建输出");
    if (file)
    {
        fputs("output", file);
        fclose(file);
    }

    CMKPlan *plan = cmk_plan_create();
    CMKTargetInfo info;
    memset(&info, 0, sizeof(info));
    cmk_string_set(&info.name, "T", 1);
    CMKTargetDraft draft;
    memset(&draft, 0, sizeof(draft));
    draft.info = &info;
    cmk_value_set_list(&draft.depends_on, CMK_VALUE_TARGET_REF);
    cmk_value_set_list(&draft.inputs, CMK_VALUE_STRING);
    cmk_value_set_list(&draft.outputs, CMK_VALUE_STRING);
    cmk_value_set_list(&draft.actions, CMK_VALUE_ACTION);
    CMKValue value;
    cmk_value_init(&value);
    cmk_value_set_string(&value, "out/cmk_cache_in", 16);
    cmk_list_append(draft.inputs.as.list, &value);
    cmk_value_clear(&value);
    cmk_value_set_string(&value, "out/cmk_cache_out", 17);
    cmk_list_append(draft.outputs.as.list, &value);
    cmk_value_clear(&value);
    cmk_value_set_action(&value, CMK_ACTION_EXEC);
    cmk_string_set(&value.as.action->program, "cc", 2);
    cmk_list_append(draft.actions.as.list, &value);
    cmk_value_clear(&value);

    CMKDiagnostic diagnostic;
    cmk_diagnostic_init(&diagnostic);
    CC_CHECK(
        cmk_plan_add_draft(plan, "m", 1, &draft, &diagnostic),
        "追加目标"
    );
    CC_CHECK(
        cmk_plan_freeze(plan, "m", 1, "T", 1, &diagnostic),
        "冻结"
    );
    CMKCache *cache = cmk_cache_create(
        "out/cmk_cache_test.cache", &diagnostic
    );
    CC_CHECK(cache != NULL, "缓存创建");
    CCBOOL skip = CCTRUE;
    CC_CHECK(
        cmk_cache_should_skip(cache, plan, 0, CCFALSE, &skip),
        "首次查询"
    );
    CC_CHECK(!skip, "首次未命中");
    CC_CHECK(cmk_cache_signature(plan, 0) != 0, "签名非零");
    CC_CHECK(cmk_cache_record(cache, plan, 0), "记录签名");
    CC_CHECK(
        cmk_cache_should_skip(cache, plan, 0, CCFALSE, &skip),
        "再次查询"
    );
    CC_CHECK(skip, "二次命中");
    CC_CHECK(
        cmk_cache_should_skip(cache, plan, 0, CCTRUE, &skip) && !skip,
        "-B 绕过缓存"
    );

    file = fopen("out/cmk_cache_in", "wb");
    if (file)
    {
        fputs("changed", file);
        fclose(file);
    }
    CC_CHECK(
        cmk_cache_should_skip(cache, plan, 0, CCFALSE, &skip) && !skip,
        "输入变化失效"
    );

    remove("out/cmk_cache_out");
    CC_CHECK(
        cmk_cache_should_skip(cache, plan, 0, CCFALSE, &skip) && !skip,
        "输出缺失失效"
    );

    cmk_cache_destroy(cache);
    cmk_plan_destroy(plan);
    cmk_value_clear(&draft.depends_on);
    cmk_value_clear(&draft.inputs);
    cmk_value_clear(&draft.outputs);
    cmk_value_clear(&draft.actions);
    cmk_string_clear(&info.name);
    cmk_diagnostic_clear(&diagnostic);
    remove("out/cmk_cache_test.cache");
    remove("out/cmk_cache_in");
    CC_CASE_END();
}
