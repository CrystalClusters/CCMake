/*
 * @Description: MC 各扫描阶段共享的基础边界扫描规则
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mc_scan_util.h"

CCBOOL mc_scan_string_end(
    const char *data,
    CCUINT64 size,
    CCUINT64 quote_pos,
    CCUINT64 *end
)
{
    if (!data || !end || quote_pos >= size || data[quote_pos] != '"')
        return CCFALSE;

    CCUINT64 i = quote_pos + 1;
    while (i < size)
    {
        if (data[i] == '\0')
            return CCFALSE;
        if (data[i] == '"')
        {
            *end = i;
            return CCTRUE;
        }
        i++;
    }
    return CCFALSE;
}

CCBOOL mc_scan_builtin_end(
    const char *data,
    CCUINT64 size,
    CCUINT64 open_pos,
    CCUINT64 *end
)
{
    if (!data || !end || open_pos >= size || data[open_pos] != '{')
        return CCFALSE;

    CCUINT32 depth = 1;
    CCUINT64 i = open_pos + 1;
    while (i < size)
    {
        char c = data[i];
        if (c == '\0')
            return CCFALSE;
        if (c == '"')
        {
            CCUINT64 quote_end = 0;
            if (!mc_scan_string_end(data, size, i, &quote_end))
                return CCFALSE;
            i = quote_end + 1;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}')
        {
            depth--;
            if (depth == 0)
            {
                *end = i;
                return CCTRUE;
            }
        }
        i++;
    }
    return CCFALSE;
}
