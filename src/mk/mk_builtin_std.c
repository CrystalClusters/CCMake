/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MK 标准内建函数
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "mk_builtin.h"
#include "mk_executor.h"
#include "mk_runtime.h"
#include "mk_safe.h"

#include "mc/mc_internal.h"

#include <ccalloc.h>
#include <cc_dyn.h>
#include <cc_wildcard.h>
#include <ccfiles.h>
#include <color_print.h>
#include "ccmake_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef CC_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MK_BUILTIN_STD_COMMENT CC_DBG_REL_TAG(".mk 标准内建", NULL)

/* ============================================================================
 * 参数与文本
 * ========================================================================== */

static CCBOOL builtin_arg_count(
    MKBuiltinCall *call, CCUINT32 min, CCUINT32 max
)
{
    if (call->argc < min || call->argc > max)
    {
        return mk_builtin_error(
            call, "参数数量错误，实际 %u", call->argc
        );
    }
    return CCTRUE;
}

static CCBOOL builtin_expect(
    MKBuiltinCall *call, CCUINT32 index,
    MKValueType type, MKValue **out
)
{
    if (index >= call->argc)
        return mk_builtin_error(call, "缺少第 %u 个参数", index + 1);
    if (call->args[index].type != type)
    {
        return mk_builtin_error(
            call, "第 %u 个参数需要 %s，实际 %s",
            index + 1, mk_value_type_name(type),
            mk_value_type_name(call->args[index].type)
        );
    }
    if (out)
        *out = &call->args[index];
    return CCTRUE;
}

static CCBOOL builtin_expect_string_list(
    MKBuiltinCall *call, CCUINT32 index, MKValue **out
)
{
    if (index >= call->argc)
        return mk_builtin_error(call, "缺少第 %u 个参数", index + 1);
    MKValue *value = &call->args[index];
    if (value->type != MK_VALUE_LIST || !value->as.list ||
        value->as.list->element_type != MK_VALUE_STRING)
    {
        return mk_builtin_error(
            call, "第 %u 个参数需要 list<string>", index + 1
        );
    }
    CCUINT32 count = mk_list_count(value->as.list);
    for (CCUINT32 i = 0; i < count; i++)
    {
        const MKValue *item = mk_list_at(value->as.list, i);
        if (!item || item->type != MK_VALUE_STRING)
            return mk_builtin_error(
                call, "第 %u 个参数含有非字符串元素", index + 1
            );
    }
    if (out)
        *out = value;
    return CCTRUE;
}

static CCBOOL builtin_format(
    MKBuiltinCall *call, MKString *out
)
{
    if (call->argc < 1)
        return mk_builtin_error(call, "需要格式字符串");

    const MKValue *fmt_value = &call->args[0];
    if (fmt_value->type != MK_VALUE_STRING)
        return mk_builtin_error(call, "格式参数必须是 string");

    const char *fmt = fmt_value->as.string.data;
    CCUINT32 len = fmt_value->as.string.len;
    CCUINT32 arg = 1;
    CCUINT32 literal = 0;

    for (CCUINT32 i = 0; i < len; i++)
    {
        if (fmt[i] != '%')
            continue;

        if (!mk_string_append(out, fmt + literal, i - literal))
            return mk_builtin_error(call, "格式化内存分配失败");
        literal = i + 1;
        if (i + 1 >= len)
            return mk_builtin_error(call, "格式字符串以 %% 结尾");

        char spec = fmt[++i];
        literal = i + 1;
        if (spec == '%')
        {
            if (!mk_string_append(out, "%", 1))
                return mk_builtin_error(call, "格式化内存分配失败");
            continue;
        }

        if (arg >= call->argc)
            return mk_builtin_error(call, "格式参数不足");

        const MKValue *value = &call->args[arg++];
        char text[64];
        int n = 0;
        switch (spec)
        {
        case 's':
            if (value->type != MK_VALUE_STRING)
                return mk_builtin_error(call, "%%s 需要 string 参数");
            if (!mk_string_append(
                    out, value->as.string.data, value->as.string.len
                ))
                return mk_builtin_error(call, "格式化内存分配失败");
            break;

        case 'd':
            if (value->type == MK_VALUE_INT)
                n = snprintf(
                    text, sizeof(text), "%lld",
                    (long long)value->as.integer
                );
            else if (value->type == MK_VALUE_BOOL)
                n = snprintf(
                    text, sizeof(text), "%d",
                    value->as.boolean ? 1 : 0
                );
            else
                return mk_builtin_error(call, "%%d 需要 int 或 bool 参数");
            if (n <= 0 || !mk_string_append(out, text, (CCUINT32)n))
                return mk_builtin_error(call, "格式化内存分配失败");
            break;

        case 'f':
            if (value->type != MK_VALUE_FLOAT)
                return mk_builtin_error(call, "%%f 需要 float 参数");
            n = snprintf(text, sizeof(text), "%g", value->as.floating);
            if (n <= 0 || !mk_string_append(out, text, (CCUINT32)n))
                return mk_builtin_error(call, "格式化内存分配失败");
            break;

        case 'v':
        {
            MKString rendered;
            mk_string_init(&rendered);
            if (!mk_value_to_string(value, &rendered))
                return mk_builtin_error(call, "值文本化失败");
            CCBOOL ok = mk_string_append(
                out, rendered.data, rendered.len
            );
            mk_string_clear(&rendered);
            if (!ok)
                return mk_builtin_error(call, "格式化内存分配失败");
            break;
        }

        default:
            return mk_builtin_error(
                call, "不支持的格式说明符 %%%c", spec
            );
        }
    }

    if (literal < len &&
        !mk_string_append(out, fmt + literal, len - literal))
        return mk_builtin_error(call, "格式化内存分配失败");
    return CCTRUE;
}

static CCBOOL builtin_log(
    MKBuiltinCall *call, const char *prefix, CCBOOL noreturn
)
{
    MKString text;
    mk_string_init(&text);
    if (!builtin_format(call, &text))
    {
        mk_string_clear(&text);
        return CCFALSE;
    }

    if (noreturn)
    {
        ccmake_write_utf8_color(text.data, "1");
        mk_string_clear(&text);
        return CCTRUE;
    }

    ccmake_write_utf8_color(
        text.data, strcmp(prefix, "warn") == 0 ? "3" : "6"
    );
    mk_string_clear(&text);
    return CCTRUE;
}

static CCBOOL builtin_print(MKBuiltinCall *call)
{
    MKString text;
    mk_string_init(&text);
    if (!builtin_format(call, &text))
    {
        mk_string_clear(&text);
        return CCFALSE;
    }
    ccmake_write_utf8(text.data ? text.data : "");
    mk_string_clear(&text);
    return CCTRUE;
}

static CCBOOL builtin_echo(MKBuiltinCall *call)
{
    MKString text;
    mk_string_init(&text);
    if (!builtin_format(call, &text))
    {
        mk_string_clear(&text);
        return CCFALSE;
    }
    ccmake_write_utf8(text.data ? text.data : "");
    ccmake_write_utf8("\n");
    mk_string_clear(&text);
    return CCTRUE;
}

/* ============================================================================
 * 纯计算与查询
 * ========================================================================== */

static CCBOOL builtin_len(MKBuiltinCall *call)
{
    if (call->argc != 1 || call->args[0].type != MK_VALUE_LIST)
        return mk_builtin_error(call, "len 需要一个 list 参数");
    return mk_value_set_int(
        call->out, (CCINT64)mk_list_count(call->args[0].as.list)
    );
}

static CCBOOL builtin_join(MKBuiltinCall *call)
{
    MKValue *list_value = NULL;
    MKValue *sep = NULL;
    if (!builtin_expect_string_list(call, 0, &list_value) ||
        !builtin_expect(call, 1, MK_VALUE_STRING, &sep))
        return CCFALSE;

    MKString out;
    mk_string_init(&out);
    CCUINT32 count = mk_list_count(list_value->as.list);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (i > 0 && !mk_string_append(
                &out, sep->as.string.data, sep->as.string.len
            ))
            goto Failed;
        const MKValue *item = mk_list_at(list_value->as.list, i);
        if (!item || !mk_string_append(
                &out, item->as.string.data, item->as.string.len
            ))
            goto Failed;
    }
    if (!mk_value_set_string(call->out, out.data, out.len))
        goto Failed;
    mk_string_clear(&out);
    return CCTRUE;

Failed:
    mk_string_clear(&out);
    return mk_builtin_error(call, "join 内存分配失败");
}

static CCBOOL builtin_subst(MKBuiltinCall *call)
{
    MKValue *from = NULL;
    MKValue *to = NULL;
    MKValue *text = NULL;
    if (!builtin_expect(call, 0, MK_VALUE_STRING, &from) ||
        !builtin_expect(call, 1, MK_VALUE_STRING, &to) ||
        !builtin_expect(call, 2, MK_VALUE_STRING, &text))
        return CCFALSE;

    MKString out;
    mk_string_init(&out);
    if (from->as.string.len == 0)
    {
        if (!mk_string_append(
                &out, text->as.string.data, text->as.string.len
            ))
            goto Failed;
        if (!mk_value_set_string(call->out, out.data, out.len))
            goto Failed;
        mk_string_clear(&out);
        return CCTRUE;
    }

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
            if (!mk_string_append(
                    &out, to->as.string.data, to->as.string.len
                ))
                goto Failed;
            pos += from->as.string.len;
        }
        else
        {
            if (!mk_string_append(&out, data + pos, 1))
                goto Failed;
            pos++;
        }
    }
    if (!mk_value_set_string(call->out, out.data, out.len))
        goto Failed;
    mk_string_clear(&out);
    return CCTRUE;

Failed:
    mk_string_clear(&out);
    return mk_builtin_error(call, "subst 内存分配失败");
}

static CCBOOL patsubst_match(
    const char *pattern, CCUINT32 pattern_len,
    const char *word, CCUINT32 word_len,
    CCBOOL *has_wildcard,
    CCUINT32 *prefix_len, CCUINT32 *suffix_len
)
{
    const char *percent = memchr(pattern, '%', pattern_len);
    *has_wildcard = percent ? CCTRUE : CCFALSE;
    if (!percent)
    {
        if (pattern_len != word_len ||
            memcmp(pattern, word, word_len) != 0)
            return CCFALSE;
        *prefix_len = 0;
        *suffix_len = 0;
        return CCTRUE;
    }

    *prefix_len = (CCUINT32)(percent - pattern);
    *suffix_len = pattern_len - *prefix_len - 1;
    if (word_len < *prefix_len + *suffix_len)
        return CCFALSE;
    if (*prefix_len > 0 &&
        memcmp(pattern, word, *prefix_len) != 0)
        return CCFALSE;
    if (*suffix_len > 0 &&
        memcmp(
            pattern + *prefix_len + 1,
            word + word_len - *suffix_len,
            *suffix_len
        ) != 0)
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL patsubst_append_word(
    MKString *out,
    const char *word, CCUINT32 word_len,
    const char *replacement, CCUINT32 replacement_len,
    CCUINT32 prefix_len, CCUINT32 suffix_len
)
{
    const char *percent = memchr(
        replacement, '%', replacement_len
    );
    if (!percent)
        return mk_string_append(
            out, replacement, replacement_len
        );
    CCUINT32 stem_len = word_len - prefix_len - suffix_len;
    CCUINT32 head = (CCUINT32)(percent - replacement);
    if (!mk_string_append(out, replacement, head))
        return CCFALSE;
    if (!mk_string_append(out, word + prefix_len, stem_len))
        return CCFALSE;
    return mk_string_append(
        out, percent + 1, replacement_len - head - 1
    );
}

static CCBOOL builtin_patsubst(MKBuiltinCall *call)
{
    MKValue *pattern = NULL;
    MKValue *replacement = NULL;
    MKValue *text = NULL;
    if (!builtin_expect(call, 0, MK_VALUE_STRING, &pattern) ||
        !builtin_expect(call, 1, MK_VALUE_STRING, &replacement) ||
        !builtin_expect(call, 2, MK_VALUE_STRING, &text))
        return CCFALSE;

    MKString out;
    mk_string_init(&out);
    CCUINT32 pos = 0;
    CCBOOL first = CCTRUE;
    while (pos < text->as.string.len)
    {
        while (pos < text->as.string.len &&
               isspace((unsigned char)text->as.string.data[pos]))
            pos++;
        if (pos >= text->as.string.len)
            break;
        CCUINT32 start = pos;
        while (pos < text->as.string.len &&
               !isspace((unsigned char)text->as.string.data[pos]))
            pos++;

        CCUINT32 prefix_len = 0;
        CCUINT32 suffix_len = 0;
        CCBOOL has_wildcard = CCFALSE;
        CCBOOL matched = patsubst_match(
            pattern->as.string.data, pattern->as.string.len,
            text->as.string.data + start, pos - start,
            &has_wildcard, &prefix_len, &suffix_len
        );
        if (!first && !mk_string_append(&out, " ", 1))
            goto Failed;
        if (matched && has_wildcard)
        {
            if (!patsubst_append_word(
                    &out,
                    text->as.string.data + start, pos - start,
                    replacement->as.string.data,
                    replacement->as.string.len,
                    prefix_len, suffix_len
                ))
                goto Failed;
        }
        else if (matched)
        {
            if (!mk_string_append(
                    &out,
                    replacement->as.string.data,
                    replacement->as.string.len
                ))
                goto Failed;
        }
        else if (!mk_string_append(
                &out, text->as.string.data + start, pos - start
            ))
            goto Failed;
        first = CCFALSE;
    }
    if (!mk_value_set_string(call->out, out.data, out.len))
        goto Failed;
    mk_string_clear(&out);
    return CCTRUE;

Failed:
    mk_string_clear(&out);
    return mk_builtin_error(call, "patsubst 内存分配失败");
}

static CCBOOL builtin_basename(MKBuiltinCall *call)
{
    MKValue *path = NULL;
    if (!builtin_expect(call, 0, MK_VALUE_STRING, &path))
        return CCFALSE;

    const char *data = path->as.string.data;
    CCUINT32 len = path->as.string.len;
    while (len > 0 && (data[len - 1] == '/' || data[len - 1] == '\\'))
        len--;
    CCUINT32 start = len;
    while (start > 0 && data[start - 1] != '/' && data[start - 1] != '\\')
        start--;
    if (len == 0)
        return mk_value_set_string(call->out, "", 0);
    return mk_value_set_string(
        call->out, data + start, len - start
    );
}

static CCBOOL builtin_dirname(MKBuiltinCall *call)
{
    MKValue *path = NULL;
    if (!builtin_expect(call, 0, MK_VALUE_STRING, &path))
        return CCFALSE;

    const char *data = path->as.string.data;
    CCUINT32 len = path->as.string.len;
    while (len > 0 && (data[len - 1] == '/' || data[len - 1] == '\\'))
        len--;
    CCUINT32 cut = len;
    while (cut > 0 && data[cut - 1] != '/' && data[cut - 1] != '\\')
        cut--;
    if (cut == 0)
        return mk_value_set_string(call->out, ".", 1);
    while (cut > 1 && (data[cut - 1] == '/' || data[cut - 1] == '\\'))
        cut--;
    return mk_value_set_string(call->out, data, cut);
}

static CCBOOL builtin_host_info(MKBuiltinCall *call)
{
    MKValue *key = NULL;
    if (!builtin_expect(call, 0, MK_VALUE_STRING, &key))
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
        return mk_builtin_error(call, "未知的系统信息键");

    return mk_value_set_string(
        call->out, value, (CCUINT32)strlen(value)
    );
}

typedef struct mk_find_entry {
    char *name;
    CCFsType type;
} MKFindEntry;

typedef struct mk_find_list {
    CCDyn entries;   /* MKFindEntry[] */
    CCBOOL failed;
} MKFindList;

static CCINT32 find_list_callback(void *data, void *user)
{
    CCFsEntry *entry = (CCFsEntry *)data;
    MKFindList *list = (MKFindList *)user;
    if (!entry || !entry->name)
        return 0;

    MKFindEntry item;
    memset(&item, 0, sizeof(item));
    size_t len = strlen(entry->name);
    item.name = ccalloc(
        NULL, (CCUINT64)len + 1,
        CC_DBG_REL_TAG(".mk find 条目", NULL)
    );
    if (!item.name)
    {
        list->failed = CCTRUE;
        return 1;
    }
    memcpy(item.name, entry->name, len + 1);
    item.type = entry->type;
    if (!mk_dyn_push(
            &list->entries, (CCUINT32)sizeof(MKFindEntry),
            (CCINT32)sizeof(MKFindEntry), &item
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
    MKFindEntry *items = (MKFindEntry *)entries->array;
    CCUINT32 count =
        entries->size / (CCUINT32)sizeof(MKFindEntry);
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
        CC_DBG_REL_TAG(".mk find 路径", NULL)
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
        CC_DBG_REL_TAG(".mk find 结果", NULL)
    );
    if (!copy)
        return CCFALSE;
    memcpy(copy, path, len + 1);
    if (!mk_dyn_push(
            results, (CCUINT32)sizeof(char *),
            (CCINT32)sizeof(char *), &copy
        ))
    {
        ccalloc(copy, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

typedef struct mk_find_dir {
    char *vpath;
    char *rel;
    CCUINT32 depth;
} MKFindDir;

static void find_dirs_clear(CCDyn *stack)
{
    MKFindDir *dirs = (MKFindDir *)stack->array;
    CCUINT32 count = stack->size / (CCUINT32)sizeof(MKFindDir);
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
    MKFindDir dir;
    memset(&dir, 0, sizeof(dir));
    size_t vlen = strlen(vpath);
    size_t rlen = strlen(rel);
    if (vlen > (size_t)-1 - 1 || rlen > (size_t)-1 - 1)
        return CCFALSE;
    dir.vpath = ccalloc(
        NULL, (CCUINT64)vlen + 1,
        CC_DBG_REL_TAG(".mk find 目录", NULL)
    );
    dir.rel = ccalloc(
        NULL, (CCUINT64)rlen + 1,
        CC_DBG_REL_TAG(".mk find 相对路径", NULL)
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
    if (!mk_dyn_push(
            stack, (CCUINT32)sizeof(MKFindDir),
            (CCINT32)sizeof(MKFindDir), &dir
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
    if (!find_push_dir(&dirs, "/mk_find_root", "", 0))
        return CCFALSE;

    CCBOOL ok = CCTRUE;
    while (dirs.size >= (CCUINT32)sizeof(MKFindDir) && ok)
    {
        MKFindDir *array = (MKFindDir *)dirs.array;
        CCUINT32 count = dirs.size / (CCUINT32)sizeof(MKFindDir);
        MKFindDir dir = array[count - 1];
        if (cc_dyn_core_pop(
                &dirs, (CCUINT32)sizeof(MKFindDir),
                (CCINT32)sizeof(MKFindDir), &dir
            ) != 0)
        {
            ok = CCFALSE;
            break;
        }

        MKFindList list;
        memset(&list, 0, sizeof(list));
        if (ccfiles_list(fs, dir.vpath, find_list_callback, &list) != CC_FS_OK ||
            list.failed)
        {
            find_entries_clear(&list.entries);
            ok = CCFALSE;
        }
        else
        {
            MKFindEntry *entries = (MKFindEntry *)list.entries.array;
            CCUINT32 entry_count =
                list.entries.size / (CCUINT32)sizeof(MKFindEntry);
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

static char *find_absolute_path(const char *path, const char *work_dir)
{
    if (!path || !path[0])
        path = ".";
    char *cwd = NULL;
    const char *base = work_dir;
    if (!base || !base[0])
    {
        cwd = mc_path_cwd(CC_DBG_REL_TAG(".mk find 根目录", NULL));
        if (!cwd)
            return NULL;
        base = cwd;
    }
    char *absolute = mc_path_normalize(
        path, base, CC_DBG_REL_TAG(".mk find 根目录", NULL)
    );
    if (cwd)
        ccalloc(cwd, 0, NULL);
    return absolute;
}

static CCBOOL builtin_find_files(MKBuiltinCall *call)
{
    MKValue *pattern = NULL;
    MKValue *roots = NULL;
    if (!builtin_arg_count(call, 2, 2) ||
        !builtin_expect(call, 0, MK_VALUE_STRING, &pattern) ||
        !builtin_expect_string_list(call, 1, &roots))
        return CCFALSE;

    CCDyn results;
    memset(&results, 0, sizeof(results));

    for (CCUINT32 root_index = 0;
         root_index < mk_list_count(roots->as.list); root_index++)
    {
        const MKValue *root =
            mk_list_at(roots->as.list, root_index);
        if (!root || root->type != MK_VALUE_STRING)
        {
            mk_builtin_error(call, "find_files 的 roots 必须是字符串列表");
            goto Failed;
        }

        CCFiles *fs = ccfiles_create();
        if (!fs)
        {
            mk_builtin_error(call, "find_files 文件系统上下文分配失败");
            goto Failed;
        }
        char *absolute_root = NULL;
        const char *work_dir =
            mk_runtime_get_work_dir(call->runtime);
        const char *source_path = root->as.string.data;
        absolute_root = find_absolute_path(source_path, work_dir);
        if (!absolute_root)
        {
            ccfiles_destroy(fs);
            mk_builtin_error(
                call, "find_files 无法规范化根目录：%s",
                root->as.string.data
            );
            goto Failed;
        }
        CCINT32 mount = ccfiles_mount(
            fs, "/mk_find_root", absolute_root,
            CC_FS_MOUNT_DIR
        );
        ccalloc(absolute_root, 0, NULL);
        if (mount != CC_FS_OK)
        {
            ccfiles_destroy(fs);
            mk_builtin_error(
                call, "find_files 无法挂载根目录：%s",
                root->as.string.data
            );
            goto Failed;
        }
        const MKExecutionPolicy *policy =
            mk_runtime_get_policy(call->runtime);
        CCUINT32 max_depth =
            policy ? policy->max_find_depth : 0;
        CCBOOL ok = find_scan_tree(
            fs, pattern->as.string.data, root->as.string.data,
            &results, max_depth
        );
        ccfiles_destroy(fs);
        if (!ok)
        {
            mk_builtin_error(
                call, "find_files 目录遍历失败或超过查找深度"
            );
            goto Failed;
        }
    }

    find_sort_unique(&results);
    if (!mk_value_set_list(call->out, MK_VALUE_STRING))
    {
        mk_builtin_error(call, "find_files 结果分配失败");
        goto Failed;
    }
    char **items = (char **)results.array;
    CCUINT32 count = results.size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        MKValue item;
        mk_value_init(&item);
        if (!mk_value_set_string(
                &item, items[i], (CCUINT32)strlen(items[i])
            ) ||
            !mk_list_append(call->out->as.list, &item))
        {
            mk_value_clear(&item);
            mk_builtin_error(call, "find_files 结果复制失败");
            goto Failed;
        }
        mk_value_clear(&item);
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
    MKBuiltinCall *call, MKString *dst, MKValue *src
)
{
    if (!mk_string_set(dst, src->as.string.data, src->as.string.len))
        return mk_builtin_error(call, "动作字符串分配失败");
    return CCTRUE;
}

static void action_clear_strings(MKAction *action)
{
    mk_string_clear(&action->program);
    mk_string_clear(&action->cwd);
    mk_string_clear(&action->from);
    mk_string_clear(&action->to);
    mk_string_clear(&action->path);
}

static CCBOOL execute_action(MKBuiltinCall *call, MKAction *action)
{
    MKExecutor *executor = mk_executor_create(
        mk_runtime_get_policy(call->runtime), call->diagnostic
    );
    if (!executor)
        return mk_builtin_error(call, "Executor 创建失败");

    MKExecutorResult result;
    CCBOOL ok = mk_executor_run_actions(executor, action, 1, &result);
    if (!ok || !result.success)
        ok = mk_builtin_error(
            call, "%s",
            result.message ? result.message : "动作执行失败"
        );
    if (result.output.len > 0)
    {
        color_print("%s", result.output.data);
    }
    mk_executor_result_clear(&result);
    mk_executor_destroy(executor);
    action_clear_strings(action);
    return ok;
}

static CCBOOL builtin_command(MKBuiltinCall *call)
{
    if (!builtin_arg_count(call, 2, 4))
        return CCFALSE;

    MKValue *program = NULL;
    MKValue *args = NULL;
    if (!builtin_expect(call, 0, MK_VALUE_STRING, &program) ||
        !builtin_expect_string_list(call, 1, &args))
        return CCFALSE;

    MKValue *cwd = NULL;
    MKValue *env = NULL;
    if (call->argc >= 3 &&
        !builtin_expect(call, 2, MK_VALUE_STRING, &cwd))
        return CCFALSE;
    if (call->argc >= 4 &&
        !builtin_expect_string_list(call, 3, &env))
        return CCFALSE;

    MKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = MK_ACTION_EXEC;
    action.args = args->as.list;
    action.env = env ? env->as.list : NULL;
    if (!action_set_string(call, &action.program, program))
        return CCFALSE;
    if (cwd && !action_set_string(call, &action.cwd, cwd))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_mkdir(MKBuiltinCall *call)
{
    MKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, MK_VALUE_STRING, &path))
        return CCFALSE;
    MKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = MK_ACTION_MKDIR;
    if (!action_set_string(call, &action.path, path))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_remove(MKBuiltinCall *call)
{
    MKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, MK_VALUE_STRING, &path))
        return CCFALSE;
    MKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = MK_ACTION_REMOVE;
    if (!action_set_string(call, &action.path, path))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_copy_move(MKBuiltinCall *call, MKActionKind kind)
{
    MKValue *from = NULL;
    MKValue *to = NULL;
    if (!builtin_arg_count(call, 2, 2) ||
        !builtin_expect(call, 0, MK_VALUE_STRING, &from) ||
        !builtin_expect(call, 1, MK_VALUE_STRING, &to))
        return CCFALSE;
    MKAction action;
    memset(&action, 0, sizeof(action));
    action.kind = kind;
    if (!action_set_string(call, &action.from, from) ||
        !action_set_string(call, &action.to, to))
        return CCFALSE;
    return execute_action(call, &action);
}

static CCBOOL builtin_copy(MKBuiltinCall *call)
{
    return builtin_copy_move(call, MK_ACTION_COPY);
}

static CCBOOL builtin_move(MKBuiltinCall *call)
{
    return builtin_copy_move(call, MK_ACTION_MOVE);
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
    MKBuiltinCall *call, const char *path, CCBOOL *ok
)
{
    *ok = CCTRUE;
    const char *work_dir = mk_runtime_get_work_dir(call->runtime);
    if (!work_dir || builtin_path_is_absolute(path))
        return NULL;
    char *joined = find_join(work_dir, path);
    if (!joined)
    {
        *ok = CCFALSE;
        (void)mk_builtin_error(call, "文件路径分配失败");
    }
    return joined;
}

static CCBOOL builtin_file_stat(
    MKBuiltinCall *call, const char *path,
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

static CCBOOL builtin_file_exists(MKBuiltinCall *call)
{
    MKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, MK_VALUE_STRING, &path))
        return CCFALSE;

    struct stat st;
    CCBOOL exists = CCFALSE;
    if (!builtin_file_stat(call, path->as.string.data, &st, &exists))
        return CCFALSE;
    return mk_value_set_bool(
        call->out, (exists && S_ISREG(st.st_mode)) ? CCTRUE : CCFALSE
    );
}

static CCBOOL builtin_file_mtime(MKBuiltinCall *call)
{
    MKValue *path = NULL;
    if (!builtin_arg_count(call, 1, 1) ||
        !builtin_expect(call, 0, MK_VALUE_STRING, &path))
        return CCFALSE;

    struct stat st;
    CCBOOL exists = CCFALSE;
    if (!builtin_file_stat(call, path->as.string.data, &st, &exists))
        return CCFALSE;
    return mk_value_set_int(
        call->out, exists ? (CCINT64)st.st_mtime : (CCINT64)-1
    );
}

static CCBOOL builtin_file_time_ns(
    MKBuiltinCall *call, const char *path,
    CCUINT64 *time_ns, CCBOOL *exists
)
{
    CCBOOL ok = CCTRUE;
    char *joined = builtin_resolve_path(call, path, &ok);
    if (!ok)
        return CCFALSE;
    const char *resolved = joined ? joined : path;
#ifdef CC_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA data;
    *exists = GetFileAttributesExA(
        resolved, GetFileExInfoStandard, &data
    ) ? CCTRUE : CCFALSE;
    if (*exists)
    {
        ULARGE_INTEGER value;
        value.LowPart = data.ftLastWriteTime.dwLowDateTime;
        value.HighPart = data.ftLastWriteTime.dwHighDateTime;
        *time_ns = (CCUINT64)value.QuadPart * 100u;
    }
#else
    struct stat st;
    *exists = stat(resolved, &st) == 0 ? CCTRUE : CCFALSE;
    if (*exists)
        *time_ns = (CCUINT64)st.st_mtim.tv_sec * 1000000000u +
            (CCUINT64)st.st_mtim.tv_nsec;
#endif
    if (joined)
        ccalloc(joined, 0, NULL);
    return CCTRUE;
}

static CCBOOL builtin_file_newer(MKBuiltinCall *call)
{
    MKValue *left = NULL;
    MKValue *right = NULL;
    if (!builtin_arg_count(call, 2, 2) ||
        !builtin_expect(call, 0, MK_VALUE_STRING, &left) ||
        !builtin_expect(call, 1, MK_VALUE_STRING, &right))
        return CCFALSE;

    CCUINT64 left_time = 0;
    CCUINT64 right_time = 0;
    CCBOOL left_exists = CCFALSE;
    CCBOOL right_exists = CCFALSE;
    if (!builtin_file_time_ns(
            call, left->as.string.data, &left_time, &left_exists
        ) ||
        !builtin_file_time_ns(
            call, right->as.string.data, &right_time, &right_exists
        ))
        return CCFALSE;

    CCBOOL newer = (left_exists && right_exists &&
        left_time > right_time) ? CCTRUE : CCFALSE;
    return mk_value_set_bool(call->out, newer);
}

/* ============================================================================
 * 注册表
 * ========================================================================== */

static const MKBuiltinArg len_args[] = {
    {"list", {MK_VALUE_LIST, MK_VALUE_VOID}, CCFALSE}
};
static const MKBuiltinArg join_args[] = {
    {"list", {MK_VALUE_LIST, MK_VALUE_STRING}, CCFALSE},
    {"sep", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE}
};
static const MKBuiltinArg subst_args[] = {
    {"from", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE},
    {"to", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE},
    {"text", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE}
};
static const MKBuiltinArg patsubst_args[] = {
    {"pattern", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE},
    {"replacement", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE},
    {"text", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE}
};
static const MKBuiltinArg one_string_args[] = {
    {"value", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE}
};
static const MKBuiltinArg find_files_args[] = {
    {"pattern", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE},
    {"roots", {MK_VALUE_LIST, MK_VALUE_STRING}, CCFALSE}
};
static const MKBuiltinArg command_args[] = {
    {"program", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE},
    {"args", {MK_VALUE_LIST, MK_VALUE_STRING}, CCFALSE},
    {"cwd", {MK_VALUE_STRING, MK_VALUE_VOID}, CCTRUE},
    {"env", {MK_VALUE_LIST, MK_VALUE_STRING}, CCTRUE}
};
static const MKBuiltinArg two_string_args[] = {
    {"from", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE},
    {"to", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE}
};
static const MKBuiltinArg format_args[] = {
    {"fmt", {MK_VALUE_STRING, MK_VALUE_VOID}, CCFALSE}
};

static CCBOOL builtin_info(MKBuiltinCall *call)
{
    return builtin_log(call, "info", CCFALSE);
}

static CCBOOL builtin_warn(MKBuiltinCall *call)
{
    return builtin_log(call, "warn", CCFALSE);
}

static CCBOOL builtin_error_log(MKBuiltinCall *call)
{
    return builtin_log(call, "error", CCTRUE);
}

#define MK_RET_VOID_ {MK_VALUE_VOID, MK_VALUE_VOID}
#define MK_RET_INT_ {MK_VALUE_INT, MK_VALUE_VOID}
#define MK_RET_BOOL_ {MK_VALUE_BOOL, MK_VALUE_VOID}
#define MK_RET_STRING_ {MK_VALUE_STRING, MK_VALUE_VOID}
#define MK_RET_LIST_STRING_ {MK_VALUE_LIST, MK_VALUE_STRING}

#define MK_BUILTIN_SPEC(name_, sig_, args_, min_, max_, ret_, flags_, fn_) \
    { \
        MK_BUILTIN_ABI_VERSION, name_, sig_, args_, min_, max_, \
        ret_, flags_, fn_ \
    }

static const MKBuiltinSpec standard_specs[] = {
    MK_BUILTIN_SPEC(
        "len", "len(list<T>) -> int", len_args, 1, 1,
        MK_RET_INT_, MK_BUILTIN_PURE, builtin_len
    ),
    MK_BUILTIN_SPEC(
        "join", "join(list<string>, string) -> string", join_args, 2, 2,
        MK_RET_STRING_, MK_BUILTIN_PURE, builtin_join
    ),
    MK_BUILTIN_SPEC(
        "subst", "subst(string, string, string) -> string",
        subst_args, 3, 3,
        MK_RET_STRING_, MK_BUILTIN_PURE, builtin_subst
    ),
    MK_BUILTIN_SPEC(
        "patsubst", "patsubst(string, string, string) -> string",
        patsubst_args, 3, 3,
        MK_RET_STRING_, MK_BUILTIN_PURE, builtin_patsubst
    ),
    MK_BUILTIN_SPEC(
        "basename", "basename(string) -> string", one_string_args, 1, 1,
        MK_RET_STRING_, MK_BUILTIN_PURE, builtin_basename
    ),
    MK_BUILTIN_SPEC(
        "dirname", "dirname(string) -> string", one_string_args, 1, 1,
        MK_RET_STRING_, MK_BUILTIN_PURE, builtin_dirname
    ),
    MK_BUILTIN_SPEC(
        "host_info", "host_info(string) -> string",
        one_string_args, 1, 1,
        MK_RET_STRING_,
        MK_BUILTIN_PURE | MK_BUILTIN_QUERY_HOST, builtin_host_info
    ),
    MK_BUILTIN_SPEC(
        "find_files", "find_files(string, list<string>) -> list<string>",
        find_files_args, 2, 2,
        MK_RET_LIST_STRING_,
        MK_BUILTIN_READ_FS, builtin_find_files
    ),
    MK_BUILTIN_SPEC(
        "file_exists", "file_exists(string) -> bool",
        one_string_args, 1, 1,
        MK_RET_BOOL_, MK_BUILTIN_READ_FS, builtin_file_exists
    ),
    MK_BUILTIN_SPEC(
        "file_mtime", "file_mtime(string) -> int",
        one_string_args, 1, 1,
        MK_RET_INT_, MK_BUILTIN_READ_FS, builtin_file_mtime
    ),
    MK_BUILTIN_SPEC(
        "file_newer", "file_newer(string, string) -> bool",
        two_string_args, 2, 2,
        MK_RET_BOOL_, MK_BUILTIN_READ_FS, builtin_file_newer
    ),
    MK_BUILTIN_SPEC(
        "command", "command(string, list<string> [, string [, list<string>]]) -> void",
        command_args, 2, 4,
        MK_RET_VOID_,
        MK_BUILTIN_BUILD_ACTION, builtin_command
    ),
    MK_BUILTIN_SPEC(
        "mkdir", "mkdir(string) -> void", one_string_args, 1, 1,
        MK_RET_VOID_,
        MK_BUILTIN_BUILD_ACTION, builtin_mkdir
    ),
    MK_BUILTIN_SPEC(
        "remove", "remove(string) -> void", one_string_args, 1, 1,
        MK_RET_VOID_,
        MK_BUILTIN_BUILD_ACTION, builtin_remove
    ),
    MK_BUILTIN_SPEC(
        "copy", "copy(string, string) -> void", two_string_args, 2, 2,
        MK_RET_VOID_,
        MK_BUILTIN_BUILD_ACTION, builtin_copy
    ),
    MK_BUILTIN_SPEC(
        "move", "move(string, string) -> void", two_string_args, 2, 2,
        MK_RET_VOID_,
        MK_BUILTIN_BUILD_ACTION, builtin_move
    ),
    MK_BUILTIN_SPEC(
        "info", "info(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        MK_RET_VOID_, MK_BUILTIN_VARIADIC, builtin_info
    ),
    MK_BUILTIN_SPEC(
        "print", "print(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        MK_RET_VOID_, MK_BUILTIN_VARIADIC, builtin_print
    ),
    MK_BUILTIN_SPEC(
        "echo", "echo(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        MK_RET_VOID_, MK_BUILTIN_VARIADIC, builtin_echo
    ),
    MK_BUILTIN_SPEC(
        "warn", "warn(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        MK_RET_VOID_, MK_BUILTIN_VARIADIC, builtin_warn
    ),
    MK_BUILTIN_SPEC(
        "error", "error(string, ...) -> void", format_args, 1, 0xFFFFFFFFu,
        MK_RET_VOID_,
        MK_BUILTIN_VARIADIC, builtin_error_log
    )
};

CCBOOL mk_builtin_register_standard(MKBuiltinRegistry *registry)
{
    if (!registry)
        return CCFALSE;

    for (CCUINT32 i = 0;
         i < (CCUINT32)(sizeof(standard_specs) / sizeof(standard_specs[0]));
         i++)
    {
        if (!mk_builtin_register(registry, &standard_specs[i]))
            return CCFALSE;
    }
    return CCTRUE;
}
