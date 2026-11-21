/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .ccm 文件定位使用的宿主路径工具
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccm_internal.h"

#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef CC_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif

#define CC_CCM_PATH_COMMENT CC_DBG_REL_TAG("CCM 路径", NULL)

/* 把一段字节追加到动态缓冲；末尾统一补 0，返回可交给 ccalloc 释放的字符串 */
static CCBOOL path_buf_put(CCDyn *buf, const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (cc_dyn_core_push(buf, 1, 1, (void *)&text[i]) != 0)
            return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL path_buf_putc(CCDyn *buf, char c)
{
    return (cc_dyn_core_push(buf, 1, 1, &c) == 0) ? CCTRUE : CCFALSE;
}

static char *path_buf_finish(CCDyn *buf)
{
    char zero = '\0';
    if (cc_dyn_core_push(buf, 1, 1, &zero) != 0)
    {
        if (buf->array)
            ccalloc(buf->array, 0, NULL);
        memset(buf, 0, sizeof(*buf));
        return NULL;
    }
    return (char *)buf->array;
}

static char *path_dup_range(const char *text, size_t len)
{
    char *out = ccalloc(NULL, (CCUINT64)len + 1, CC_CCM_PATH_COMMENT);
    if (!out)
        return NULL;
    memcpy(out, text, len);
    out[len] = '\0';
    return out;
}

CCBOOL ccm_path_is_absolute(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;

#ifdef CC_WINDOWS
    if (path[0] == '/' || path[0] == '\\')
        return CCTRUE;
    return (isalpha((unsigned char)path[0]) && path[1] == ':' &&
            (path[2] == '/' || path[2] == '\\')) ? CCTRUE : CCFALSE;
#else
    return (path[0] == '/') ? CCTRUE : CCFALSE;
#endif
}

CCBOOL ccm_path_equal(const char *a, const char *b)
{
    if (!a || !b)
        return CCFALSE;
#ifdef CC_WINDOWS
    return (_stricmp(a, b) == 0) ? CCTRUE : CCFALSE;
#else
    return (strcmp(a, b) == 0) ? CCTRUE : CCFALSE;
#endif
}

/*
 * 规范化流程：
 *   1. Windows 把 '\' 统一成 '/'；
 *   2. 相对路径拼接 base；
 *   3. 识别 POSIX 根 / Windows 盘符根 / UNC 前缀，其余部分按组件处理；
 *   4. 丢弃空组件与 "."，用组件栈处理 ".."；
 *   5. 重拼并补 0。
 * 返回的字符串由 ccalloc 分配，调用方用 ccalloc(p, 0, NULL) 释放。
 */
char *ccm_path_normalize(const char *path, const char *base, const char *comment)
{
    (void)comment;

    if (!path || !path[0])
        return NULL;

    size_t path_len = strlen(path);
    char *work = ccalloc(NULL, (CCUINT64)path_len + 1, CC_CCM_PATH_COMMENT);
    if (!work)
        return NULL;

    for (size_t i = 0; i <= path_len; i++)
    {
#ifdef CC_WINDOWS
        work[i] = (path[i] == '\\') ? '/' : path[i];
#else
        work[i] = path[i];
#endif
    }

#ifdef CC_WINDOWS
    /* 盘符相对路径（C:foo / C:）语义含糊，直接拒绝 */
    if (isalpha((unsigned char)work[0]) && work[1] == ':')
    {
        if (work[2] != '/')
        {
            ccalloc(work, 0, NULL);
            return NULL;
        }
    }
#endif

    if (!ccm_path_is_absolute(work))
    {
        if (!base || !base[0])
        {
            ccalloc(work, 0, NULL);
            return NULL;
        }

        size_t base_len = strlen(base);
        CCBOOL base_has_slash = (base[base_len - 1] == '/') ? CCTRUE : CCFALSE;
        size_t total = base_len + (base_has_slash ? 0 : 1) + path_len + 1;
        char *combined = ccalloc(NULL, (CCUINT64)total, CC_CCM_PATH_COMMENT);
        if (!combined)
        {
            ccalloc(work, 0, NULL);
            return NULL;
        }
        memcpy(combined, base, base_len);
        size_t pos = base_len;
        if (!base_has_slash)
            combined[pos++] = '/';
        memcpy(combined + pos, work, path_len + 1);
        ccalloc(work, 0, NULL);
        work = combined;
        path_len = total - 1;
    }

    /* 前缀识别：prefix 指向 work 内一段不会被组件切分改写的区间 */
    size_t prefix_len = 0;
    size_t start = 0;
#ifdef CC_WINDOWS
    if (isalpha((unsigned char)work[0]) && work[1] == ':' && work[2] == '/')
    {
        work[0] = (char)toupper((unsigned char)work[0]);
        prefix_len = 3;
        start = 3;
    }
    else if (work[0] == '/' && work[1] == '/')
    {
        size_t server_end = 2;
        while (work[server_end] && work[server_end] != '/')
            server_end++;
        if (server_end == 2 || work[server_end] == '\0')
        {
            ccalloc(work, 0, NULL);
            return NULL;
        }
        size_t share_end = server_end + 1;
        while (work[share_end] && work[share_end] != '/')
            share_end++;
        if (share_end == server_end + 1)
        {
            ccalloc(work, 0, NULL);
            return NULL;
        }
        prefix_len = share_end;
        start = share_end;
    }
    else if (work[0] == '/')
    {
        prefix_len = 1;
        start = 1;
    }
    else
    {
        ccalloc(work, 0, NULL);
        return NULL;
    }
#else
    if (work[0] == '/')
    {
        prefix_len = 1;
        start = 1;
    }
    else
    {
        ccalloc(work, 0, NULL);
        return NULL;
    }
#endif

    CCDyn comps;
    memset(&comps, 0, sizeof(comps));
    size_t pos = start;
    while (pos < path_len)
    {
        while (pos < path_len && work[pos] == '/')
            pos++;
        if (pos >= path_len)
            break;

        size_t begin = pos;
        while (pos < path_len && work[pos] != '/')
            pos++;
        size_t clen = pos - begin;
        if (clen == 0)
            continue;

        /* 先记下组件，再把分隔符改成 0 并越过它，组件指针即可直接当 C 字符串用 */
        if (pos < path_len)
        {
            work[pos] = '\0';
            pos++;
        }
        char *comp = work + begin;

        if (strcmp(comp, ".") == 0)
            continue;
        if (strcmp(comp, "..") == 0)
        {
            CCUINT32 count = comps.size / (CCUINT32)sizeof(char *);
            if (count > 0)
            {
                comps.size -= (CCUINT32)sizeof(char *);
                char **slots = (char **)comps.array;
                slots[count - 1] = NULL;
            }
            continue;
        }

#ifdef CC_WINDOWS
        if (clen > 0 && (comp[clen - 1] == ' ' || comp[clen - 1] == '.'))
        {
            ccalloc(work, 0, NULL);
            if (comps.array)
                ccalloc(comps.array, 0, NULL);
            return NULL;
        }
#endif

        if (cc_dyn_core_push(&comps, (CCUINT32)sizeof(char *),
                             (CCINT32)sizeof(char *), &comp) != 0)
        {
            ccalloc(work, 0, NULL);
            if (comps.array)
                ccalloc(comps.array, 0, NULL);
            return NULL;
        }
    }

    CCDyn out;
    memset(&out, 0, sizeof(out));
    if (!path_buf_put(&out, work, prefix_len))
    {
        ccalloc(work, 0, NULL);
        if (comps.array)
            ccalloc(comps.array, 0, NULL);
        return NULL;
    }

    CCUINT32 comp_count = comps.size / (CCUINT32)sizeof(char *);
    char **slots = (char **)comps.array;
    CCBOOL prefix_has_slash = (prefix_len > 0 && work[prefix_len - 1] == '/')
                                  ? CCTRUE : CCFALSE;
    for (CCUINT32 i = 0; i < comp_count; i++)
    {
        if (i > 0 || !prefix_has_slash)
        {
            if (!path_buf_putc(&out, '/'))
            {
                ccalloc(work, 0, NULL);
                ccalloc(comps.array, 0, NULL);
                if (out.array)
                    ccalloc(out.array, 0, NULL);
                return NULL;
            }
        }
        if (!path_buf_put(&out, slots[i], strlen(slots[i])))
        {
            ccalloc(work, 0, NULL);
            ccalloc(comps.array, 0, NULL);
            if (out.array)
                ccalloc(out.array, 0, NULL);
            return NULL;
        }
    }

    ccalloc(work, 0, NULL);
    if (comps.array)
        ccalloc(comps.array, 0, NULL);
    return path_buf_finish(&out);
}

char *ccm_path_dirname(const char *path, const char *comment)
{
    (void)comment;
    if (!path || !path[0])
        return NULL;

    size_t len = strlen(path);
    size_t slash = len;
    while (slash > 0)
    {
        slash--;
        if (path[slash] == '/')
            break;
    }
    if (slash == 0 && path[0] != '/')
        return NULL;

    /* 根："/"、"X:/" 原样返回 */
    if (slash == 0)
        return path_dup_range(path, 1);
#ifdef CC_WINDOWS
    if (slash == 2 && isalpha((unsigned char)path[0]) && path[1] == ':')
        return path_dup_range(path, 3);
#endif
    return path_dup_range(path, slash);
}

char *ccm_path_cwd(const char *comment)
{
    (void)comment;
    CCUINT32 cap = 256;
    char *buf = ccalloc(NULL, cap, CC_CCM_PATH_COMMENT);
    if (!buf)
        return NULL;

    for (;;)
    {
#ifdef CC_WINDOWS
        if (_getcwd(buf, (int)cap) != NULL)
            break;
#else
        if (getcwd(buf, (size_t)cap) != NULL)
            break;
#endif
        if (errno != ERANGE)
        {
            ccalloc(buf, 0, NULL);
            return NULL;
        }
        if (cap > 0x40000000u)
        {
            ccalloc(buf, 0, NULL);
            return NULL;
        }
        CCUINT32 next = cap * 2;
        char *grown = ccalloc(buf, next, CC_CCM_PATH_COMMENT);
        if (!grown)
        {
            ccalloc(buf, 0, NULL);
            return NULL;
        }
        buf = grown;
        cap = next;
    }

    char *norm = ccm_path_normalize(buf, NULL, CC_CCM_PATH_COMMENT);
    ccalloc(buf, 0, NULL);
    return norm;
}

CC_CCM_HOST_TYPE ccm_host_path_type(const char *path)
{
    if (!path || !path[0])
        return CC_CCM_HOST_MISSING;

#ifdef CC_WINDOWS
    struct _stat64 st;
    if (_stat64(path, &st) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            return CC_CCM_HOST_MISSING;
        if (errno == EACCES)
            return CC_CCM_HOST_DENIED;
        return CC_CCM_HOST_MISSING;
    }
    if ((st.st_mode & _S_IFDIR) != 0)
        return CC_CCM_HOST_DIR;
    if ((st.st_mode & _S_IFREG) != 0)
        return CC_CCM_HOST_FILE;
    return CC_CCM_HOST_OTHER;
#else
    struct stat st;
    if (stat(path, &st) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            return CC_CCM_HOST_MISSING;
        if (errno == EACCES)
            return CC_CCM_HOST_DENIED;
        return CC_CCM_HOST_MISSING;
    }
    if (S_ISDIR(st.st_mode))
        return CC_CCM_HOST_DIR;
    if (S_ISREG(st.st_mode))
        return CC_CCM_HOST_FILE;
    return CC_CCM_HOST_OTHER;
#endif
}
