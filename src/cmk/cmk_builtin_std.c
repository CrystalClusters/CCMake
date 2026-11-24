/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CMK 标准内建函数
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "cmk_builtin.h"
#include "cmk_executor.h"
#include "cmk_runtime.h"
#include "cmk_safe.h"

#include <ccalloc.h>
#include <cc_dyn.h>
#include <cc_wildcard.h>
#include <ccfiles.h>
#include <color_print.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifndef CC_WINDOWS
#include <unistd.h>
#endif

#define CMK_BUILTIN_STD_COMMENT CC_DBG_REL_TAG(".cmk 标准内建", NULL)

/* ============================================================================
 * 参数与文本
 * ========================================================================== */

static CCBOOL builtin_arg_count(
    CMKBuiltinCall *call, CCUINT32 min, CCUINT32 max
)
{
    if (call->argc < min || call->argc > max)
    {
        return cmk_builtin_error(
            call, "参数数量错误，实际 %u", call->argc
        );
    }
    return CCTRUE;
}

static CCBOOL builtin_expect(
    CMKBuiltinCall *call, CCUINT32 index,
    CMKValueType type, CMKValue **out
)
{
    if (index >= call->argc)
        return cmk_builtin_error(call, "缺少第 %u 个参数", index + 1);
    if (call->args[index].type != type)
    {
        return cmk_builtin_error(
            call, "第 %u 个参数需要 %s，实际 %s",
            index + 1, cmk_value_type_name(type),
            cmk_value_type_name(call->args[index].type)
        );
    }
    if (out)
        *out = &call->args[index];
    return CCTRUE;
}

static CCBOOL builtin_expect_string_list(
    CMKBuiltinCall *call, CCUINT32 index, CMKValue **out
)
{
    if (index >= call->argc)
        return cmk_builtin_error(call, "缺少第 %u 个参数", index + 1);
    CMKValue *value = &call->args[index];
    if (value->type != CMK_VALUE_LIST || !value->as.list ||
        value->as.list->element_type != CMK_VALUE_STRING)
    {
        return cmk_builtin_error(
            call, "第 %u 个参数需要 list<string>", index + 1
        );
    }
    CCUINT32 count = cmk_list_count(value->as.list);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const CMKValue *item = cmk_list_at(value->as.list, i);
        if (!item || item->type != CMK_VALUE_STRING)
            return cmk_builtin_error(
                call, "第 %u 个参数含有非字符串元素", index + 1
            );
    }
    if (out)
        *out = value;
    return CCTRUE;
}

static CCBOOL builtin_format(
    CMKBuiltinCall *call, CMKString *out
)
{
    if (call->argc < 1)
        return cmk_builtin_error(call, "需要格式字符串");

    const CMKValue *fmt_value = &call->args[0];
    if (fmt_value->type != CMK_VALUE_STRING)
        return cmk_builtin_error(call, "格式参数必须是 string");

    const char *fmt = fmt_value->as.string.data;
    CCUINT32 len = fmt_value->as.string.len;
    CCUINT32 arg = 1;
    CCUINT32 literal = 0;

    for (CCUINT32 i = 0; i < len; i++)
    {
        if (fmt[i] != '%')
            continue;

        if (!cmk_string_append(out, fmt + literal, i - literal))
            return cmk_builtin_error(call, "格式化内存分配失败");
        literal = i + 1;
        if (i + 1 >= len)
            return cmk_builtin_error(call, "格式字符串以 %% 结尾");

        char spec = fmt[++i];
        literal = i + 1;
        if (spec == '%')
        {
            if (!cmk_string_append(out, "%", 1))
                return cmk_builtin_error(call, "格式化内存分配失败");
            continue;
        }

        if (arg >= call->argc)
            return cmk_builtin_error(call, "格式参数不足");

        const CMKValue *value = &call->args[arg++];
        char text[64];
        int n = 0;
        switch (spec)
        {
        case 's':
            if (value->type != CMK_VALUE_STRING)
                return cmk_builtin_error(call, "%%s 需要 string 参数");
            if (!cmk_string_append(
                    out, value->as.string.data, value->as.string.len
                ))
                return cmk_builtin_error(call, "格式化内存分配失败");
            break;

        case 'd':
            if (value->type == CMK_VALUE_INT)
                n = snprintf(
                    text, sizeof(text), "%lld",
                    (long long)value->as.integer
                );
            else if (value->type == CMK_VALUE_BOOL)
                n = snprintf(
                    text, sizeof(text), "%d",
                    value->as.boolean ? 1 : 0
                );
            else
                return cmk_builtin_error(call, "%%d 需要 int 或 bool 参数");
            if (n <= 0 || !cmk_string_append(out, text, (CCUINT32)n))
                return cmk_builtin_error(call, "格式化内存分配失败");
            break;

        case 'f':
            if (value->type != CMK_VALUE_FLOAT)
                return cmk_builtin_error(call, "%%f 需要 float 参数");
            n = snprintf(text, sizeof(text), "%g", value->as.floating);
            if (n <= 0 || !cmk_string_append(out, text, (CCUINT32)n))
                return cmk_builtin_error(call, "格式化内存分配失败");
            break;

        case 'v':
        {
            CMKString rendered;
            cmk_string_init(&rendered);
            if (!cmk_value_to_string(value, &rendered))
                return cmk_builtin_error(call, "值文本化失败");
            CCBOOL ok = cmk_string_append(
                out, rendered.data, rendered.len
            );
            cmk_string_clear(&rendered);
            if (!ok)
                return cmk_builtin_error(call, "格式化内存分配失败");
            break;
        }

        default:
            return cmk_builtin_error(
                call, "不支持的格式说明符 %%%c", spec
            );
        }
    }

    if (literal < len &&
        !cmk_string_append(out, fmt + literal, len - literal))
        return cmk_builtin_error(call, "格式化内存分配失败");
    return CCTRUE;
}

static CCBOOL builtin_log(
    CMKBuiltinCall *call, const char *prefix, CCBOOL noreturn
)
{
    CMKString text;
    cmk_string_init(&text);
    if (!builtin_format(call, &text))
    {
        cmk_string_clear(&text);
        return CCFALSE;
    }

    if (noreturn)
    {
        (void)cmk_builtin_error(call, "%s", text.data);
        cmk_string_clear(&text);
        cmk_runtime_mark_fatal(call->runtime);
        return CCFALSE;
    }

    color_print(
        CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT) "[%s] %s\n",
        prefix, text.data
    );
    cmk_string_clear(&text);
    return CCTRUE;
}

/* ============================================================================
 * 纯计算与查询
 * ========================================================================== */

static CCBOOL builtin_len(CMKBuiltinCall *call)
{
    if (call->argc != 1 || call->args[0].type != CMK_VALUE_LIST)
        return cmk_builtin_error(call, "len 需要一个 list 参数");
    return cmk_value_set_int(
        call->out, (CCINT64)cmk_list_count(call->args[0].as.list)
    );
}

static CCBOOL builtin_join(CMKBuiltinCall *call)
{
    CMKValue *list_value = NULL;
    CMKValue *sep = NULL;
    if (!builtin_expect_string_list(call, 0, &list_value) ||
        !builtin_expect(call, 1, CMK_VALUE_STRING, &sep))
        return CCFALSE;

    CMKString out;
    cmk_string_init(&out);
    CCUINT32 count = cmk_list_count(list_value->as.list);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (i > 0 && !cmk_string_append(
                &out, sep->as.string.data, sep->as.string.len
            ))
            goto Failed;
        const CMKValue *item = cmk_list_at(list_value->as.list, i);
        if (!item || !cmk_string_append(
                &out, item->as.string.data, item->as.string.len
            ))
            goto Failed;
    }
    if (!cmk_value_set_string(call->out, out.data, out.len))
        goto Failed;
    cmk_string_clear(&out);
    return CCTRUE;

Failed:
    cmk_string_clear(&out);
    return cmk_builtin_error(call, "join 内存分配失败");
}

static CCBOOL builtin_subst(CMKBuiltinCall *call)
{
    CMKValue *text = NULL;
    CMKValue *from = NULL;
    CMKValue *to = NULL;
    if (!builtin_expect(call, 0, CMK_VALUE_STRING, &text) ||
        !builtin_expect(call, 1, CMK_VALUE_STRING, &from) ||
        !builtin_expect(call, 2, CMK_VALUE_STRING, &to))
        return CCFALSE;
    if (from->as.string.len == 0)
        return cmk_builtin_error(call, "subst 的 from 不能为空");

    CMKString out;
    cmk_string_init(&out);
    const char *data = text->as.string.data;
    CCUINT32 len = text->as.string.len;
    CCUINT32 pos = 0;
    while (pos < len)
    {
        if (pos + from->as.string.len <= len &&
            memcmp(
                data + pos, from->as.string.data,
                from->as.string.len
            ) == 0)
        {
            if (!cmk_string_append(
                    &out, to->as.string.data, to->as.string.len
                ))
                goto Failed;
            pos += from->as.string.len;
        }
        else
        {
            if (!cmk_string_append(&out, data + pos, 1))
                goto Failed;
            pos++;
        }
    }
    if (!cmk_value_set_string(call->out, out.data, out.len))
        goto Failed;
    cmk_string_clear(&out);
    return CCTRUE;

Failed:
    cmk_string_clear(&out);
    return cmk_builtin_error(call, "subst 内存分配失败");
}

static CCBOOL builtin_basename(CMKBuiltinCall *call)
{
    CMKValue *path = NULL;
    if (!builtin_expect(call, 0, CMK_VALUE_STRING, &path))
        return CCFALSE;

    const char *data = path->as.string.data;
    CCUINT32 len = path->as.string.len;
    while (len > 0 && (data[len - 1] == '/' || data[len - 1] == '\\'))
        len--;
    CCUINT32 start = len;
    while (start > 0 && data[start - 1] != '/' && data[start - 1] != '\\')
        start--;
    if (len == 0)
        return cmk_value_set_string(call->out, "", 0);
    return cmk_value_set_string(
        call->out, data + start, len - start
    );
}

static CCBOOL builtin_dirname(CMKBuiltinCall *call)
{
    CMKValue *path = NULL;
    if (!builtin_expect(call, 0, CMK_VALUE_STRING, &path))
        return CCFALSE;

    const char *data = path->as.string.data;
    CCUINT32 len = path->as.string.len;
    while (len > 0 && (data[len - 1] == '/' || data[len - 1] == '\\'))
        len--;
    CCUINT32 cut = len;
    while (cut > 0 && data[cut - 1] != '/' && data[cut - 1] != '\\')
        cut--;
    if (cut == 0)
        return cmk_value_set_string(call->out, ".", 1);
    while (cut > 1 && (data[cut - 1] == '/' || data[cut - 1] == '\\'))
        cut--;
    return cmk_value_set_string(call->out, data, cut);
}

static CCBOOL builtin_host_info(CMKBuiltinCall *call)
{
    CMKValue *key = NULL;
    if (!builtin_expect(call, 0, CMK_VALUE_STRING, &key))
        return CCFALSE;

    const char *value = NULL;
    char cpu_buffer[32];
    if (key->as.string.len == 2 &&
        memcmp(key->as.string.data, "os", 2) == 0)
    {
#ifdef CC_WINDOWS
        value = "windows";
#elif defined CC_ANDROID
        value = "android";
#elif defined CC_LINUX
        value = "linux";
#else
        value = "unknown";
#endif
    }
    else if (key->as.string.len == 4 &&
             memcmp(key->as.string.data, "arch", 4) == 0)
    {
#if defined(CC_ARCH_AMD64)
        value = "amd64";
#elif defined(CC_ARCH_ARM64)
        value = "arm64";
#elif defined(CC_ARCH_X86)
        value = "x86";
#elif defined(CC_ARCH_ARM)
        value = "arm";
#else
        value = "unknown";
#endif
    }
    else if (key->as.string.len == 9 &&
             memcmp(key->as.string.data, "cpu_count", 9) == 0)
    {
        const char *env_cpus = getenv("NUMBER_OF_PROCESSORS");
        if (env_cpus && env_cpus[0])
        {
            snprintf(cpu_buffer, sizeof(cpu_buffer), "%s", env_cpus);
            value = cpu_buffer;
        }
        else
        {
#ifndef CC_WINDOWS
            long cpus = sysconf(_SC_NPROCESSORS_ONLN);
            if (cpus <= 0)
                cpus = 1;
            snprintf(cpu_buffer, sizeof(cpu_buffer), "%ld", cpus);
            value = cpu_buffer;
#else
            value = "1";
#endif
        }
    }
    else if (key->as.string.len == 14 &&
             memcmp(key->as.string.data, "path_separator", 14) == 0)
    {
#ifdef CC_WINDOWS
        value = "\\";
#else
        value = "/";
#endif
    }
    else
        return cmk_builtin_error(call, "未知的系统信息键");

    return cmk_value_set_string(
        call->out, value, (CCUINT32)strlen(value)
    );
}

typedef struct cmk_find_entry {
    char *name;
    CCFsType type;
} CMKFindEntry;

typedef struct cmk_find_list {
    CCDyn entries;   /* CMKFindEntry[] */
    CCBOOL failed;
} CMKFindList;

static CCINT32 find_list_callback(void *data, void *user)
{
    CCFsEntry *entry = (CCFsEntry *)data;
    CMKFindList *list = (CMKFindList *)user;
    if (!entry || !entry->name)
        return 0;

    CMKFindEntry item;
    memset(&item, 0, sizeof(item));
    size_t len = strlen(entry->name);
    item.name = ccalloc(
        NULL, (CCUINT64)len + 1,
        CC_DBG_REL_TAG(".cmk find 条目", NULL)
    );
    if (!item.name)
    {
        list->failed = CCTRUE;
        return 1;
    }
    memcpy(item.name, entry->name, len + 1);
    item.type = entry->type;
    if (!cmk_dyn_push(
            &list->entries, (CCUINT32)sizeof(CMKFindEntry),
            (CCINT32)sizeof(CMKFindEntry), &item
        ))
    {
        ccalloc(item.name, 0, NULL);
        list->failed = CCTRUE;
        return 1;
    }
    return 0;
}

static void find_entries_clear(CCDyn *entries)
{
    CMKFindEntry *items = (CMKFindEntry *)entries->array;
    CCUINT32 count =
        entries->size / (CCUINT32)sizeof(CMKFindEntry);
    for (CCUINT32 i = 0; i < count; i++)
        ccalloc(items[i].name, 0, NULL);
    if (entries->array)
        ccalloc(entries->array, 0, NULL);
    memset(entries, 0, sizeof(*entries));
}

static char *find_join(const char *left, const char *right)
{
    size_t left_len = left ? strlen(left) : 0;
    while (left_len > 0 &&
           (left[left_len - 1] == '/' || left[left_len - 1] == '\\'))
        left_len--;
    size_t right_len = right ? strlen(right) : 0;
    if (left_len > (size_t)-1 - right_len - 3)
        return NULL;
    size_t need = left_len + (left_len > 0 && right_len > 0 ? 1 : 0) +
        right_len + 1;
    char *out = ccalloc(
        NULL, (CCUINT64)need,
        CC_DBG_REL_TAG(".cmk find 路径", NULL)
    );
    if (!out)
        return NULL;
    size_t pos = 0;
    if (left_len > 0)
    {
        memcpy(out, left, left_len);
        pos = left_len;
    }
    if (left_len > 0 && right_len > 0)
        out[pos++] = '/';
    if (right_len > 0)
        memcpy(out + pos, right, right_len);
    out[pos + right_len] = '\0';
    return out;
}

static CCBOOL find_add_result(CCDyn *results, const char *path)
{
    char **items = (char **)results->array;
    CCUINT32 count = results->size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (strcmp(items[i], path) == 0)
            return CCTRUE;
    }

    size_t len = strlen(path);
    char *copy = ccalloc(
        NULL, (CCUINT64)len + 1,
        CC_DBG_REL_TAG(".cmk find 结果", NULL)
    );
    if (!copy)
        return CCFALSE;
    memcpy(copy, path, len + 1);
    if (!cmk_dyn_push(
            results, (CCUINT32)sizeof(char *),
            (CCINT32)sizeof(char *), &copy
        ))
    {
        ccalloc(copy, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

typedef struct cmk_find_dir {
    char *vpath;
    char *rel;
    CCUINT32 depth;
} CMKFindDir;

static void find_dirs_clear(CCDyn *stack)
{
    CMKFindDir *dirs = (CMKFindDir *)stack->array;
    CCUINT32 count = stack->size / (CCUINT32)sizeof(CMKFindDir);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (dirs[i].vpath)
            ccalloc(dirs[i].vpath, 0, NULL);
        if (dirs[i].rel)
            ccalloc(dirs[i].rel, 0, NULL);
    }
    if (stack->array)
        ccalloc(stack->array, 0, NULL);
    memset(stack, 0, sizeof(*stack));
}

static CCBOOL find_push_dir(
    CCDyn *stack, const char *vpath, const char *rel, CCUINT32 depth
)
{
    CMKFindDir dir;
    memset(&dir, 0, sizeof(dir));
    size_t vlen = strlen(vpath);
    size_t rlen = strlen(rel);
    if (vlen > (size_t)-1 - 1 || rlen > (size_t)-1 - 1)
        return CCFALSE;
    dir.vpath = ccalloc(
        NULL, (CCUINT64)vlen + 1,
        CC_DBG_REL_TAG(".cmk find 目录", NULL)
    );
    dir.rel = ccalloc(
        NULL, (CCUINT64)rlen + 1,
        CC_DBG_REL_TAG(".cmk find 相对路径", NULL)
    );
    if (!dir.vpath || !dir.rel)
    {
        if (dir.vpath)
            ccalloc(dir.vpath, 0, NULL);
        if (dir.rel)
            ccalloc(dir.rel, 0, NULL);
        return CCFALSE;
    }
    memcpy(dir.vpath, vpath, vlen + 1);
    memcpy(dir.rel, rel, rlen + 1);
    dir.depth = depth;
    if (!cmk_dyn_push(
            stack, (CCUINT32)sizeof(CMKFindDir),
            (CCINT32)sizeof(CMKFindDir), &dir
        ))
    {
        ccalloc(dir.vpath, 0, NULL);
        ccalloc(dir.rel, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL find_scan_tree(
    CCFiles *fs, const char *pattern, const char *root,
    CCDyn *results, CCUINT32 max_depth
)
{
    CCDyn dirs;
    memset(&dirs, 0, sizeof(dirs));
    if (!find_push_dir(&dirs, "/cmk_find_root", "", 0))
        return CCFALSE;

    CCBOOL ok = CCTRUE;
    while (dirs.size >= (CCUINT32)sizeof(CMKFindDir) && ok)
    {
        CMKFindDir *array = (CMKFindDir *)dirs.array;
        CCUINT32 count = dirs.size / (CCUINT32)sizeof(CMKFindDir);
        CMKFindDir dir = array[count - 1];
        if (cc_dyn_core_pop(
                &dirs, (CCUINT32)sizeof(CMKFindDir),
                (CCINT32)sizeof(CMKFindDir), &dir
            ) != 0)
        {
            ok = CCFALSE;
            break;
        }

        CMKFindList list;
        memset(&list, 0, sizeof(list));
        if (ccfiles_list(fs, dir.vpath, find_list_callback, &list) != CC_FS_OK ||
            list.failed)
        {
            find_entries_clear(&list.entries);
            ok = CCFALSE;
        }
        else
        {
            CMKFindEntry *entries = (CMKFindEntry *)list.entries.array;
            CCUINT32 entry_count =
                list.entries.size / (CCUINT32)sizeof(CMKFindEntry);
            for (CCUINT32 i = 0; i < entry_count && ok; i++)
            {
                if (strcmp(entries[i].name, ".") == 0 ||
                    strcmp(entries[i].name, "..") == 0)
                    continue;

                if (entries[i].type == CC_FS_TYPE_DIR)
                {
                    if (max_depth != 0 && dir.depth >= max_depth)
                    {
                        ok = CCFALSE;
                        break;
                    }
                    char *child_vpath = find_join(
                        dir.vpath, entries[i].name
                    );
                    char *child_rel = find_join(
                        dir.rel, entries[i].name
                    );
                    if (!child_vpath || !child_rel ||
                        !find_push_dir(
                            &dirs, child_vpath, child_rel,
                            dir.depth + 1
                        ))
                        ok = CCFALSE;
                    if (child_vpath)
                        ccalloc(child_vpath, 0, NULL);
                    if (child_rel)
                        ccalloc(child_rel, 0, NULL);
                }
                else if (cc_wildcard(pattern, entries[i].name))
                {
                    char *child_rel = find_join(
                        dir.rel, entries[i].name
                    );
                    char *full = child_rel
                        ? find_join(root, child_rel) : NULL;
                    if (!child_rel || !full ||
                        !find_add_result(results, full))
                        ok = CCFALSE;
                    if (child_rel)
                        ccalloc(child_rel, 0, NULL);
                    if (full)
                        ccalloc(full, 0, NULL);
                }
            }
            find_entries_clear(&list.entries);
        }
        ccalloc(dir.vpath, 0, NULL);
        ccalloc(dir.rel, 0, NULL);
    }

    find_dirs_clear(&dirs);
    return ok;
}

static int find_result_compare(const void *a, const void *b)
{
    const char *left = *(const char *const *)a;
    const char *right = *(const char *const *)b;
    return strcmp(left, right);
}

static void find_sort_unique(CCDyn *results)
{
    char **items = (char **)results->array;
    CCUINT32 count = results->size / (CCUINT32)sizeof(char *);
    if (count < 2)
        return;
    qsort(items, count, sizeof(char *), find_result_compare);

    CCUINT32 write = 1;
    for (CCUINT32 read = 1; read < count; read++)
    {
        if (strcmp(items[read], items[write - 1]) == 0)
        {
            ccalloc(items[read], 0, NULL);
        }
        else
        {
            items[write++] = items[read];
        }
    }
    results->size = write * (CCUINT32)sizeof(char *);
}

static void find_results_clear(CCDyn *results)
{
    char **items = (char **)results->array;
    CCUINT32 count = results->size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
        ccalloc(items[i], 0, NULL);
    if (results->array)
        ccalloc(results->array, 0, NULL);
    memset(results, 0, sizeof(*results));
}

static char *find_absolute_path(const char *path)
{
#ifdef CC_WINDOWS
    return _fullpath(NULL, path, 0);
#else
    return realpath(path, NULL);
#endif
}

static CCBOOL find_is_absolute(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;
#ifdef CC_WINDOWS
    if (path[0] == '/' || path[0] == '\\')
        return CCTRUE;
    return (path[1] == ':' &&
            (path[2] == '/' || path[2] == '\\')) ? CCTRUE : CCFALSE;
#else
    return (path[0] == '/') ? CCTRUE : CCFALSE;
#endif
}

static CCBOOL builtin_find_files(CMKBuiltinCall *call)
{
    CMKValue *pattern = NULL;
    CMKValue *roots = NULL;
    if (!builtin_arg_count(call, 2, 2) ||
        !builtin_expect(call, 0, CMK_VALUE_STRING, &pattern) ||
        !builtin_expect_string_list(call, 1, &roots))
        return CCFALSE;

    CCDyn results;
    memset(&results, 0, sizeof(results));

    for (CCUINT32 root_index = 0;
         root_index < cmk_list_count(roots->as.list); root_index++)
    {
        const CMKValue *root =
            cmk_list_at(roots->as.list, root_index);
        if (!root || root->type != CMK_VALUE_STRING)
        {
            cmk_builtin_error(call, "find_files 的 roots 必须是字符串列表");
            goto Failed;
        }

        CCFiles *fs = ccfiles_create();
        if (!fs)
        {
            cmk_builtin_error(call, "find_files 文件系统上下文分配失败");
            goto Failed;
        }
        char *absolute_root = NULL;
        const char *work_dir =
            cmk_runtime_get_work_dir(call->runtime);
        char *joined = NULL;
        const char *source_path = root->as.string.data;
        if (work_dir && !find_is_absolute(source_path))
        {
            joined = find_join(work_dir, source_path);
            if (!joined)
            {
                ccfiles_destroy(fs);
                cmk_builtin_error(call, "find_files 路径分配失败");
                goto Failed;
            }
            source_path = joined;
        }
        absolute_root = find_absolute_path(source_path);
        if (joined)
            ccalloc(joined, 0, NULL);
        if (!absolute_root)
        {
            ccfiles_destroy(fs);
            cmk_builtin_error(
                call, "find_files 无法规范化根目录：%s",
                root->as.string.data
            );
            goto Failed;
        }
        for (char *p = absolute_root; *p; p++)
        {
            if (*p == '\\')
                *p = '/';
        }
        CCINT32 mount = ccfiles_mount(
            fs, "/cmk_find_root", absolute_root,
            CC_FS_MOUNT_DIR
        );
        free(absolute_root);
        if (mount != CC_FS_OK)
        {
            ccfiles_destroy(fs);
            cmk_builtin_error(
                call, "find_files 无法挂载根目录：%s",
                root->as.string.data
            );
            goto Failed;
        }
        const CMKExecutionPolicy *policy =
            cmk_runtime_get_policy(call->runtime);
        CCUINT32 max_depth =
            policy ? policy->max_find_depth : 0;
        CCBOOL ok = find_scan_tree(
            fs, pattern->as.string.data, root->as.string.data,
            &results, max_depth
        );
        ccfiles_destroy(fs);
        if (!ok)
        {
            cmk_builtin_error(
                call, "find_files 目录遍历失败或超过查找深度"
            );
            goto Failed;
        }
    }

    find_sort_unique(&results);
    if (!cmk_value_set_list(call->out, CMK_VALUE_STRING))
    {
        cmk_builtin_error(call, "find_files 结果分配失败");
        goto Failed;
    }
    char **items = (char **)results.array;
    CCUINT32 count = results.size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        CMKValue item;
        cmk_value_init(&item);
        if (!cmk_value_set_string(
                &item, items[i], (CCUINT32)strlen(items[i])
            ) ||
            !cmk_list_append(call->out->as.list, &item))
        {
            cmk_value_clear(&item);
            cmk_builtin_error(call, "find_files 结果复制失败");
            goto Failed;
        }
        cmk_value_clear(&item);
    }
    find_results_clear(&results);
    return CCTRUE;

Failed:
    find_results_clear(&results);
    return CCFALSE;
}

/* ============================================================================
 * 动作构造
 * ========================================================================== */


static CCBOOL action_set_string(
    CMKBuiltinCall *call, CMKString *dst, CMKValue *src
)
{
    if (!cmk_string_set(dst, src->as.string.data, src->as.string.len))
        return cmk_builtin_error(call, "动作字符串分配失败");
    return CCTRUE;
}

static void action_clear_strings(CMKAction *action)
{
    cmk_string_clear(&action->program);
    cmk_string_clear(&action->cwd);
    cmk_string_clear(&action->from);
    cmk_string_clear(&action->to);
    cmk_string_clear(&action->path);
}

static CCBOOL execute_action(CMKBuiltinCall *call, CMKAction *action)
{
    CMKExecutor *executor = cmk_executor_create(
        cmk_runtime_get_policy(call->runtime), call->diagnostic
    );
    if (!executor)
        return cmk_builtin_error(call, "Executor 创建失败");

    CMKExecutorResult result;
    CCBOOL ok = cmk_executor_run_actions(executor, action, 1, &result);
    if (!ok || !result.success)
        ok = cmk_builtin_error(
            call, "%s",
            result.message ? result.message : "动作执行失败"
        );
    if (result.output.len > 0)
    {
        fwrite(result.output.data, 1, result.output.len, stdout);
        fflush(stdout);
    }
    cmk_executor_result_clear(&result);
    cmk_executor_destroy(executor);
    action_clear_strings(action);
    return ok;
}

static CCBOOL builtin_command(CMKBuiltinCall *call)
{
    if (!builtin_arg_count(call, 2, 4))
        return CCFALSE;

    CMKValue *program = NULL;
    CMKValue *args = NULL;
    if (!builtin_expect(call, 0, CMK_VALUE_STRING, &program) ||
        !builtin_expect_string_list(call, 1, &args))
        return CCFALSE;

    CMKValue *cwd = NULL;
    CMKValue *env = NULL;
    if (call->argc >= 3 &&
        !builtin_expect(call, 2, CMK_VALUE_STRING, &cwd))
        return CCFALSE;
    if (call->argc >= 4 &&
        !builtin_expect_string_list(call, 3, &env))
        return CCFALSE;

    CMKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = CMK_ACTION_EXEC;
    action.args = args->as.list;
    action.env = env ? env->as.list : NULL;
    if (!action_set_string(call, &action.program, program))
        return CCFALSE;
    if (cwd && !action_set_string(call, &action.cwd, cwd))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_mkdir(CMKBuiltinCall *call)
{
    CMKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, CMK_VALUE_STRING, &path))
        return CCFALSE;
    CMKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = CMK_ACTION_MKDIR;
    if (!action_set_string(call, &action.path, path))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_remove(CMKBuiltinCall *call)
{
    CMKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, CMK_VALUE_STRING, &path))
        return CCFALSE;
    CMKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = CMK_ACTION_REMOVE;
    if (!action_set_string(call, &action.path, path))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_copy_move(CMKBuiltinCall *call, CMKActionKind kind)
{
    CMKValue *from = NULL;
    CMKValue *to = NULL;
    if (!builtin_arg_count(call, 2, 2) ||
        !builtin_expect(call, 0, CMK_VALUE_STRING, &from) ||
        !builtin_expect(call, 1, CMK_VALUE_STRING, &to))
        return CCFALSE;
    CMKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = kind;
    if (!action_set_string(call, &action.from, from) ||
        !action_set_string(call, &action.to, to))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_copy(CMKBuiltinCall *call)
{
    return builtin_copy_move(call, CMK_ACTION_COPY);
}

static CCBOOL builtin_move(CMKBuiltinCall *call)
{
    return builtin_copy_move(call, CMK_ACTION_MOVE);
}

/* ============================================================================
 * 文件状态
 * ========================================================================== */

static CCBOOL builtin_path_is_absolute(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;
    if (path[0] == '/' || path[0] == '\\')
        return CCTRUE;
    return (isalpha((unsigned char)path[0]) && path[1] == ':')
        ? CCTRUE : CCFALSE;
}

static char *builtin_resolve_path(
    CMKBuiltinCall *call, const char *path, CCBOOL *ok
)
{
    *ok = CCTRUE;
    const char *work_dir = cmk_runtime_get_work_dir(call->runtime);
    if (!work_dir || builtin_path_is_absolute(path))
        return NULL;
    char *joined = find_join(work_dir, path);
    if (!joined)
    {
        *ok = CCFALSE;
        (void)cmk_builtin_error(call, "文件路径分配失败");
    }
    return joined;
}

static CCBOOL builtin_file_stat(
    CMKBuiltinCall *call, const char *path,
    struct stat *st, CCBOOL *exists
)
{
    CCBOOL ok = CCTRUE;
    char *joined = builtin_resolve_path(call, path, &ok);
    if (!ok)
        return CCFALSE;
    const char *resolved = joined ? joined : path;
    *exists = (stat(resolved, st) == 0) ? CCTRUE : CCFALSE;
    if (joined)
        ccalloc(joined, 0, NULL);
    return CCTRUE;
}

static CCBOOL builtin_file_exists(CMKBuiltinCall *call)
{
    CMKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, CMK_VALUE_STRING, &path))
        return CCFALSE;

    struct stat st;
    CCBOOL exists = CCFALSE;
    if (!builtin_file_stat(call, path->as.string.data, &st, &exists))
        return CCFALSE;
    return cmk_value_set_bool(
        call->out, (exists && S_ISREG(st.st_mode)) ? CCTRUE : CCFALSE
    );
}

static CCBOOL builtin_file_mtime(CMKBuiltinCall *call)
{
    CMKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, CMK_VALUE_STRING, &path))
        return CCFALSE;

    struct stat st;
    CCBOOL exists = CCFALSE;
    if (!builtin_file_stat(call, path->as.string.data, &st, &exists))
        return CCFALSE;
    return cmk_value_set_int(
        call->out, exists ? (CCINT64)st.st_mtime : (CCINT64)-1
    );
}

static CCBOOL builtin_file_newer(CMKBuiltinCall *call)
{
    CMKValue *left = NULL;
    CMKValue *right = NULL;
    if (!builtin_arg_count(call, 2, 2) ||
        !builtin_expect(call, 0, CMK_VALUE_STRING, &left) ||
        !builtin_expect(call, 1, CMK_VALUE_STRING, &right))
        return CCFALSE;

    struct stat left_st;
    struct stat right_st;
    CCBOOL left_exists = CCFALSE;
    CCBOOL right_exists = CCFALSE;
    if (!builtin_file_stat(
            call, left->as.string.data, &left_st, &left_exists
        ) ||
        !builtin_file_stat(
            call, right->as.string.data, &right_st, &right_exists
        ))
        return CCFALSE;

    CCBOOL newer = (left_exists && right_exists &&
        left_st.st_mtime > right_st.st_mtime) ? CCTRUE : CCFALSE;
    return cmk_value_set_bool(call->out, newer);
}

/* ============================================================================
 * 注册表
 * ========================================================================== */

static const CMKBuiltinArg len_args[] = {
    {"list", {CMK_VALUE_LIST, CMK_VALUE_VOID}, CCFALSE}
};
static const CMKBuiltinArg join_args[] = {
    {"list", {CMK_VALUE_LIST, CMK_VALUE_STRING}, CCFALSE},
    {"sep", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE}
};
static const CMKBuiltinArg subst_args[] = {
    {"text", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE},
    {"from", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE},
    {"to", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE}
};
static const CMKBuiltinArg one_string_args[] = {
    {"value", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE}
};
static const CMKBuiltinArg find_files_args[] = {
    {"pattern", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE},
    {"roots", {CMK_VALUE_LIST, CMK_VALUE_STRING}, CCFALSE}
};
static const CMKBuiltinArg command_args[] = {
    {"program", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE},
    {"args", {CMK_VALUE_LIST, CMK_VALUE_STRING}, CCFALSE},
    {"cwd", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCTRUE},
    {"env", {CMK_VALUE_LIST, CMK_VALUE_STRING}, CCTRUE}
};
static const CMKBuiltinArg two_string_args[] = {
    {"from", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE},
    {"to", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE}
};
static const CMKBuiltinArg format_args[] = {
    {"fmt", {CMK_VALUE_STRING, CMK_VALUE_VOID}, CCFALSE}
};

static CCBOOL builtin_info(CMKBuiltinCall *call)
{
    return builtin_log(call, "info", CCFALSE);
}

static CCBOOL builtin_warn(CMKBuiltinCall *call)
{
    return builtin_log(call, "warn", CCFALSE);
}

static CCBOOL builtin_error_log(CMKBuiltinCall *call)
{
    return builtin_log(call, "error", CCTRUE);
}

#define CMK_RET_VOID_ {CMK_VALUE_VOID, CMK_VALUE_VOID}
#define CMK_RET_INT_ {CMK_VALUE_INT, CMK_VALUE_VOID}
#define CMK_RET_BOOL_ {CMK_VALUE_BOOL, CMK_VALUE_VOID}
#define CMK_RET_STRING_ {CMK_VALUE_STRING, CMK_VALUE_VOID}
#define CMK_RET_LIST_STRING_ {CMK_VALUE_LIST, CMK_VALUE_STRING}

#define CMK_BUILTIN_SPEC(name_, sig_, args_, min_, max_, ret_, flags_, fn_) \
    { \
        CMK_BUILTIN_ABI_VERSION, name_, sig_, args_, min_, max_, \
        ret_, flags_, fn_ \
    }

static const CMKBuiltinSpec standard_specs[] = {
    CMK_BUILTIN_SPEC(
        "len", "len(list<T>) -> int", len_args, 1, 1,
        CMK_RET_INT_, CMK_BUILTIN_PURE, builtin_len
    ),
    CMK_BUILTIN_SPEC(
        "join", "join(list<string>, string) -> string", join_args, 2, 2,
        CMK_RET_STRING_, CMK_BUILTIN_PURE, builtin_join
    ),
    CMK_BUILTIN_SPEC(
        "subst", "subst(string, string, string) -> string",
        subst_args, 3, 3,
        CMK_RET_STRING_, CMK_BUILTIN_PURE, builtin_subst
    ),
    CMK_BUILTIN_SPEC(
        "basename", "basename(string) -> string", one_string_args, 1, 1,
        CMK_RET_STRING_, CMK_BUILTIN_PURE, builtin_basename
    ),
    CMK_BUILTIN_SPEC(
        "dirname", "dirname(string) -> string", one_string_args, 1, 1,
        CMK_RET_STRING_, CMK_BUILTIN_PURE, builtin_dirname
    ),
    CMK_BUILTIN_SPEC(
        "host_info", "host_info(string) -> string",
        one_string_args, 1, 1,
        CMK_RET_STRING_,
        CMK_BUILTIN_PURE | CMK_BUILTIN_QUERY_HOST, builtin_host_info
    ),
    CMK_BUILTIN_SPEC(
        "find_files", "find_files(string, list<string>) -> list<string>",
        find_files_args, 2, 2,
        CMK_RET_LIST_STRING_,
        CMK_BUILTIN_READ_FS, builtin_find_files
    ),
    CMK_BUILTIN_SPEC(
        "file_exists", "file_exists(string) -> bool",
        one_string_args, 1, 1,
        CMK_RET_BOOL_, CMK_BUILTIN_READ_FS, builtin_file_exists
    ),
    CMK_BUILTIN_SPEC(
        "file_mtime", "file_mtime(string) -> int",
        one_string_args, 1, 1,
        CMK_RET_INT_, CMK_BUILTIN_READ_FS, builtin_file_mtime
    ),
    CMK_BUILTIN_SPEC(
        "file_newer", "file_newer(string, string) -> bool",
        two_string_args, 2, 2,
        CMK_RET_BOOL_, CMK_BUILTIN_READ_FS, builtin_file_newer
    ),
    CMK_BUILTIN_SPEC(
        "command", "command(string, list<string> [, string [, list<string>]]) -> action",
        command_args, 2, 4,
        CMK_RET_VOID_,
        CMK_BUILTIN_BUILD_ACTION, builtin_command
    ),
    CMK_BUILTIN_SPEC(
        "mkdir", "mkdir(string) -> void", one_string_args, 1, 1,
        CMK_RET_VOID_,
        CMK_BUILTIN_BUILD_ACTION, builtin_mkdir
    ),
    CMK_BUILTIN_SPEC(
        "remove", "remove(string) -> void", one_string_args, 1, 1,
        CMK_RET_VOID_,
        CMK_BUILTIN_BUILD_ACTION, builtin_remove
    ),
    CMK_BUILTIN_SPEC(
        "copy", "copy(string, string) -> void", two_string_args, 2, 2,
        CMK_RET_VOID_,
        CMK_BUILTIN_BUILD_ACTION, builtin_copy
    ),
    CMK_BUILTIN_SPEC(
        "move", "move(string, string) -> void", two_string_args, 2, 2,
        CMK_RET_VOID_,
        CMK_BUILTIN_BUILD_ACTION, builtin_move
    ),
    CMK_BUILTIN_SPEC(
        "info", "info(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        CMK_RET_VOID_, CMK_BUILTIN_VARIADIC, builtin_info
    ),
    CMK_BUILTIN_SPEC(
        "warn", "warn(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        CMK_RET_VOID_, CMK_BUILTIN_VARIADIC, builtin_warn
    ),
    CMK_BUILTIN_SPEC(
        "error", "error(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        CMK_RET_VOID_,
        CMK_BUILTIN_VARIADIC | CMK_BUILTIN_NORETURN, builtin_error_log
    )
};

CCBOOL cmk_builtin_register_standard(CMKBuiltinRegistry *registry)
{
    if (!registry)
        return CCFALSE;

    for (CCUINT32 i = 0;
         i < (CCUINT32)(sizeof(standard_specs) / sizeof(standard_specs[0]));
         i++)
    {
        if (!cmk_builtin_register(registry, &standard_specs[i]))
            return CCFALSE;
    }
    return CCTRUE;
}
