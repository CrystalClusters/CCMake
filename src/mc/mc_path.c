/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MC 宿主路径适配
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mc_internal.h"

#include <ccalloc.h>
#include <cc_encoding.h>
#include <cchost.h>
#include <ccpath.h>

#include <ctype.h>

CCBOOL mc_path_is_absolute(const char *path)
{
    return ccpath_is_absolute(path);
}

CCBOOL mc_path_equal(const char *a, const char *b)
{
    return ccpath_equal(a, b);
}

char *mc_path_normalize(
    const char *path, const char *base, const char *comment
)
{
    (void)comment;
    char *resolved = ccpath_resolve(path, base);
#ifdef CC_WINDOWS
    if (resolved && isalpha((unsigned char)resolved[0]) &&
        resolved[1] == ':')
        resolved[0] = (char)toupper((unsigned char)resolved[0]);
#endif
    return resolved;
}

char *mc_path_dirname(const char *path, const char *comment)
{
    (void)comment;
    return ccpath_parent(path);
}

char *mc_path_cwd(const char *comment)
{
    (void)comment;
    return ccpath_resolve(".", NULL);
}

CC_MC_HOST_TYPE mc_host_path_type(const char *path)
{
    char *local = path
        ? cc_utf8_to_local(path, CC_LOCAL_SYSTEM) : NULL;
    if (!local)
        return CC_MC_HOST_MISSING;
    CCHostStat stat;
    CCINT32 result = cchost_stat(local, &stat);
    ccalloc(local, 0, NULL);
    if (result != CC_HOST_OK)
        return CC_MC_HOST_MISSING;
    switch (stat.kind)
    {
    case CC_HOST_FILE:
        return CC_MC_HOST_FILE;
    case CC_HOST_DIR:
        return CC_MC_HOST_DIR;
    case CC_HOST_OTHER:
        return CC_MC_HOST_OTHER;
    case CC_HOST_DENIED:
        return CC_MC_HOST_DENIED;
    default:
        return CC_MC_HOST_MISSING;
    }
}
