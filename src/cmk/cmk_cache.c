/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 增量签名缓存（M8）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_cache.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMK_CACHE_VERSION 1
#define CMK_CACHE_COMMENT CC_DBG_REL_TAG(".cmk 缓存", NULL)

struct cmk_cache {
    char *path;
    CMKDiagnostic *diagnostic;
};

CMKCache *cmk_cache_create(const char *path, CMKDiagnostic *diagnostic)
{
    CMKCache *cache = ccalloc(
        NULL, sizeof(CMKCache), CMK_CACHE_COMMENT
    );
    if (!cache)
        return NULL;
    cache->diagnostic = diagnostic;
    if (path)
    {
        size_t len = strlen(path);
        cache->path = ccalloc(
            NULL, (CCUINT64)len + 1, CMK_CACHE_COMMENT
        );
        if (!cache->path)
        {
            ccalloc(cache, 0, NULL);
            return NULL;
        }
        memcpy(cache->path, path, len + 1);
    }
    return cache;
}

void cmk_cache_destroy(CMKCache *cache)
{
    if (!cache)
        return;
    if (cache->path)
        ccalloc(cache->path, 0, NULL);
    ccalloc(cache, 0, NULL);
}

static CCUINT64 cmk_cache_hash_bytes(
    CCUINT64 hash, const void *data, CCUINT64 len
)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (CCUINT64 i = 0; i < len; i++)
    {
        hash ^= bytes[i];
        hash *= (CCUINT64)1099511628211ULL;
    }
    return hash;
}

static CCUINT64 cmk_cache_hash_string(
    CCUINT64 hash, const char *text
)
{
    return text ? cmk_cache_hash_bytes(
        hash, text, (CCUINT64)strlen(text)
    ) : hash;
}

static CCBOOL cmk_cache_file_hash(
    const char *path, CCUINT64 *out
)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return CCFALSE;
    CCUINT64 hash = (CCUINT64)1469598103934665603ULL;
    char buffer[64 * 1024];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        hash = cmk_cache_hash_bytes(hash, buffer, read);
    CCBOOL ok = !ferror(file);
    fclose(file);
    if (ok)
        *out = hash;
    return ok;
}

static CCBOOL cmk_cache_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return CCFALSE;
    fclose(file);
    return CCTRUE;
}

CCUINT64 cmk_cache_signature(
    const CMKPlan *plan, CMKTargetId target_id
)
{
    const CMKPlanTarget *target =
        cmk_plan_target_at(plan, target_id);
    if (!target)
        return 0;
    CCUINT64 hash = (CCUINT64)1469598103934665603ULL;
    CCUINT32 version = CMK_CACHE_VERSION;
    hash = cmk_cache_hash_bytes(hash, &version, sizeof(version));
    hash = cmk_cache_hash_string(hash, target->key.module.data);
    hash = cmk_cache_hash_string(hash, ":");
    hash = cmk_cache_hash_string(hash, target->key.name.data);

    CMKString *inputs = (CMKString *)target->inputs.array;
    CCUINT32 input_count = target->inputs.size /
        (CCUINT32)sizeof(CMKString);
    for (CCUINT32 i = 0; i < input_count; i++)
    {
        CCUINT64 file_hash = 0;
        if (!cmk_cache_file_hash(inputs[i].data, &file_hash))
            return 0;
        hash = cmk_cache_hash_string(hash, "|i:");
        hash = cmk_cache_hash_string(hash, inputs[i].data);
        hash = cmk_cache_hash_bytes(hash, &file_hash, sizeof(file_hash));
    }

    CMKString *outputs = (CMKString *)target->outputs.array;
    CCUINT32 output_count = target->outputs.size /
        (CCUINT32)sizeof(CMKString);
    for (CCUINT32 i = 0; i < output_count; i++)
    {
        CCUINT64 file_hash = 0;
        if (!cmk_cache_file_hash(outputs[i].data, &file_hash))
            return 0;
        hash = cmk_cache_hash_string(hash, "|o:");
        hash = cmk_cache_hash_string(hash, outputs[i].data);
        hash = cmk_cache_hash_bytes(hash, &file_hash, sizeof(file_hash));
    }

    CMKPlanAction *actions = (CMKPlanAction *)target->actions.array;
    CCUINT32 action_count = target->actions.size /
        (CCUINT32)sizeof(CMKPlanAction);
    for (CCUINT32 i = 0; i < action_count; i++)
    {
        CMKPlanAction *action = &actions[i];
        hash = cmk_cache_hash_string(hash, "|a:");
        hash = cmk_cache_hash_string(hash, action->program.data);
        hash = cmk_cache_hash_string(hash, action->cwd.data);
        hash = cmk_cache_hash_string(hash, action->from.data);
        hash = cmk_cache_hash_string(hash, action->to.data);
        hash = cmk_cache_hash_string(hash, action->path.data);
        CMKString *argv = (CMKString *)action->argv.array;
        CCUINT32 argc = action->argv.size /
            (CCUINT32)sizeof(CMKString);
        for (CCUINT32 a = 0; a < argc; a++)
            hash = cmk_cache_hash_string(hash, argv[a].data);
        CMKString *env = (CMKString *)action->env.array;
        CCUINT32 envc = action->env.size /
            (CCUINT32)sizeof(CMKString);
        for (CCUINT32 e = 0; e < envc; e++)
            hash = cmk_cache_hash_string(hash, env[e].data);
    }
    return hash;
}

static CCBOOL cmk_cache_key(
    const CMKPlanTarget *target, char *buffer, CCUINT32 size
)
{
    int written = snprintf(
        buffer, size, "%s:%s",
        target->key.module.data, target->key.name.data
    );
    return (written > 0 && (CCUINT32)written < size) ? CCTRUE : CCFALSE;
}

static CCBOOL cmk_cache_lookup(
    const CMKCache *cache, const char *key, CCUINT64 *signature
)
{
    if (!cache || !cache->path)
        return CCFALSE;
    FILE *file = fopen(cache->path, "rb");
    if (!file)
        return CCFALSE;
    char line[4096];
    CCBOOL found = CCFALSE;
    while (fgets(line, sizeof(line), file))
    {
        char *first = strchr(line, '\t');
        if (!first)
            continue;
        *first = '\0';
        if (strcmp(line, key) == 0)
        {
            *signature = strtoull(first + 1, NULL, 16);
            found = CCTRUE;
            break;
        }
    }
    fclose(file);
    return found;
}

static CCBOOL cmk_cache_write_entry(
    const CMKCache *cache, const char *key, CCUINT64 signature
)
{
    if (!cache || !cache->path)
        return CCFALSE;
    size_t len = strlen(cache->path);
    char *temp = ccalloc(
        NULL, (CCUINT64)len + 5, CMK_CACHE_COMMENT
    );
    if (!temp)
        return CCFALSE;
    memcpy(temp, cache->path, len);
    memcpy(temp + len, ".tmp", 5);

    FILE *in = fopen(cache->path, "rb");
    FILE *out = fopen(temp, "wb");
    if (!out)
    {
        if (in)
            fclose(in);
        ccalloc(temp, 0, NULL);
        return CCFALSE;
    }
    CCBOOL ok = CCTRUE;
    char line[4096];
    if (in)
    {
        while (fgets(line, sizeof(line), in))
        {
            char *first = strchr(line, '\t');
            if (!first)
                continue;
            if ((size_t)(first - line) == strlen(key) &&
                memcmp(line, key, strlen(key)) == 0)
                continue;
            fputs(line, out);
        }
        if (ferror(in))
            ok = CCFALSE;
        fclose(in);
    }
    if (ok)
        fprintf(out, "%s\t%016llx\n", key, (unsigned long long)signature);
    if (fclose(out) != 0)
        ok = CCFALSE;
    if (ok)
    {
        remove(cache->path);
        if (rename(temp, cache->path) != 0)
        {
            /* 某些文件系统不允许覆盖重命名，退化为直接写入 */
            FILE *direct = fopen(cache->path, "wb");
            if (!direct)
                ok = CCFALSE;
            else
            {
                fprintf(
                    direct, "%s\t%016llx\n", key,
                    (unsigned long long)signature
                );
                if (fclose(direct) != 0)
                    ok = CCFALSE;
            }
        }
    }
    if (!ok)
        remove(temp);
    ccalloc(temp, 0, NULL);
    return ok;
}

CCBOOL cmk_cache_should_skip(
    CMKCache *cache, const CMKPlan *plan, CMKTargetId target_id,
    CCBOOL force, CCBOOL *skip
)
{
    if (!skip)
        return CCFALSE;
    *skip = CCFALSE;
    if (force || !cache || !cache->path)
        return CCTRUE;
    const CMKPlanTarget *target = cmk_plan_target_at(plan, target_id);
    if (!target)
        return CCFALSE;
    CMKString *outputs = (CMKString *)target->outputs.array;
    CCUINT32 output_count = target->outputs.size /
        (CCUINT32)sizeof(CMKString);
    if (output_count == 0)
        return CCTRUE;
    for (CCUINT32 i = 0; i < output_count; i++)
    {
        if (!cmk_cache_file_exists(outputs[i].data))
            return CCTRUE;
    }
    char key[1024];
    if (!cmk_cache_key(target, key, sizeof(key)))
        return CCFALSE;
    CCUINT64 cached = 0;
    if (!cmk_cache_lookup(cache, key, &cached))
        return CCTRUE;
    CCUINT64 current = cmk_cache_signature(plan, target_id);
    *skip = (current != 0 && current == cached) ? CCTRUE : CCFALSE;
    return CCTRUE;
}

CCBOOL cmk_cache_record(
    CMKCache *cache, const CMKPlan *plan, CMKTargetId target_id
)
{
    if (!cache || !cache->path)
        return CCTRUE;
    const CMKPlanTarget *target = cmk_plan_target_at(plan, target_id);
    if (!target)
        return CCFALSE;
    CMKString *outputs = (CMKString *)target->outputs.array;
    CCUINT32 output_count = target->outputs.size /
        (CCUINT32)sizeof(CMKString);
    for (CCUINT32 i = 0; i < output_count; i++)
    {
        if (!cmk_cache_file_exists(outputs[i].data))
            return CCFALSE;
    }
    CCUINT64 signature = cmk_cache_signature(plan, target_id);
    if (signature == 0)
        return CCFALSE;
    char key[1024];
    if (!cmk_cache_key(target, key, sizeof(key)))
        return CCFALSE;
    return cmk_cache_write_entry(cache, key, signature);
}
