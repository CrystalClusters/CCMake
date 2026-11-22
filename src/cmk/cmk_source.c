/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .cmk 源码缓冲区与源码位置
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_source.h"

#include <ccalloc.h>

#include <stdint.h>
#include <string.h>

#define CMK_SOURCE_COMMENT CC_DBG_REL_TAG(".cmk 源码缓冲区", NULL)

void cmk_source_init(CMKSource *source)
{
    if (!source)
        return;
    memset(source, 0, sizeof(*source));
}

void cmk_source_clear(CMKSource *source)
{
    if (!source)
        return;
    if (source->path)
        ccalloc(source->path, 0, NULL);
    if (source->data)
        ccalloc(source->data, 0, NULL);
    memset(source, 0, sizeof(*source));
}

static char *source_copy_string(const char *text)
{
    size_t len = strlen(text);
    char *copy = ccalloc(NULL, (CCUINT64)len + 1, CMK_SOURCE_COMMENT);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

CCBOOL cmk_source_set(
    CMKSource *source, const char *path,
    const char *data, CCUINT64 size
)
{
    if (!source || !path || (!data && size > 0))
        return CCFALSE;
    // 结尾 0 需要额外一个字节；size 必须能用 size_t 表示
    if (size > (CCUINT64)SIZE_MAX - 1)
        return CCFALSE;

    char *path_copy = source_copy_string(path);
    if (!path_copy)
        return CCFALSE;

    char *data_copy = ccalloc(NULL, size + 1, CMK_SOURCE_COMMENT);
    if (!data_copy)
    {
        ccalloc(path_copy, 0, NULL);
        return CCFALSE;
    }
    if (size > 0)
        memcpy(data_copy, data, (size_t)size);
    data_copy[(size_t)size] = '\0';

    cmk_source_clear(source);
    source->path = path_copy;
    source->data = data_copy;
    source->size = size;
    return CCTRUE;
}
