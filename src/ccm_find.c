/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .ccm 第二阶段 ${find} 文件发现（基于 VFS 目录遍历）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccm_internal.h"
#include "ccm_scan_util.h"

#include <cc_wildcard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ccm_find_token_list {
    CCDyn tokens; /* char* */
} CCMFindTokens;

typedef struct ccm_find_stack_item {
    char *vpath;
    char *rel;
    CCUINT32 depth;
} CCMFindStackItem;

typedef struct ccm_find_entry {
    char *name;
    CCFsType type;
} CCMFindEntry;

typedef struct ccm_find_list_ctx {
    CCDyn entries; /* CCMFindEntry[] */
} CCMFindListCtx;

static void find_tokens_free(CCMFindTokens *t)
{
    if (!t)
        return;
    char **items = (char **)t->tokens.array;
    CCUINT32 count = t->tokens.size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
        ccalloc(items[i], 0, NULL);
    if (t->tokens.array)
        ccalloc(t->tokens.array, 0, NULL);
    memset(t, 0, sizeof(*t));
}

static CCBOOL find_tokenize(
    const char *args,
    CCUINT32 len,
    CCMFindTokens *out,
    CCUINT32 max_tokens,
    const char *file,
    CCUINT32 line
)
{
    memset(out, 0, sizeof(*out));
    CCUINT32 i = 0;
    while (i < len)
    {
        while (i < len && (args[i] == ' ' || args[i] == '\t' ||
                           args[i] == '\r' || args[i] == '\n'))
            i++;
        if (i >= len)
            break;

        if (out->tokens.size / (CCUINT32)sizeof(char *) >= max_tokens)
        {
            find_tokens_free(out);
            return ccm_parse_failf(file, line, "${find} 参数数量超过限制");
        }

        CCUINT32 begin = i;
        char *text = NULL;
        if (args[i] == '"')
        {
            CCUINT64 end64 = 0;
            if (!ccm_scan_string_end(args, len, i, &end64))
            {
                find_tokens_free(out);
                return ccm_parse_failf(file, line, "${find} 参数中的字符串未闭合");
            }
            CCUINT32 end = (CCUINT32)end64;
            begin = i + 1;
            CCUINT32 n = end - begin;
            text = ccalloc(
                NULL, (CCUINT64)n + 1,
                CC_DBG_REL_TAG(".ccm find 参数", NULL)
            );
            if (!text)
            {
                find_tokens_free(out);
                return ccm_parse_failf(file, line, "内存分配失败");
            }
            memcpy(text, args + begin, n);
            text[n] = '\0';
            i = end + 1;
        }
        else
        {
            while (i < len && args[i] != ' ' && args[i] != '\t' &&
                   args[i] != '\r' && args[i] != '\n')
                i++;
            CCUINT32 n = i - begin;
            text = ccalloc(
                NULL, (CCUINT64)n + 1,
                CC_DBG_REL_TAG(".ccm find 参数", NULL)
            );
            if (!text)
            {
                find_tokens_free(out);
                return ccm_parse_failf(file, line, "内存分配失败");
            }
            memcpy(text, args + begin, n);
            text[n] = '\0';
        }

        if (cc_dyn_core_push(
                &out->tokens, (CCUINT32)sizeof(char *),
                (CCINT32)sizeof(char *), &text
            ) != 0
        )
        {
            ccalloc(text, 0, NULL);
            find_tokens_free(out);
            return ccm_parse_failf(file, line, "内存分配失败");
        }
    }
    return CCTRUE;
}

static CCBOOL find_buf_put(CCDyn *buf, const char *text, CCUINT32 len)
{
    for (CCUINT32 i = 0; i < len; i++)
    {
        if (cc_dyn_core_push(buf, 1, 1, (void *)&text[i]) != 0)
            return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL find_push_str(CCDyn *dyn, const char *text)
{
    char *copy = ccalloc(
        NULL, (CCUINT64)strlen(text) + 1,
        CC_DBG_REL_TAG(".ccm find 路径", NULL)
    );
    if (!copy)
        return CCFALSE;
    memcpy(copy, text, strlen(text) + 1);
    if (cc_dyn_core_push(
            dyn, (CCUINT32)sizeof(char *),
            (CCINT32)sizeof(char *), &copy
        ) != 0
    )
    {
        ccalloc(copy, 0, NULL);
        return CCFALSE;
    }
    return CCTRUE;
}

static CCINT32 find_add_result(CCDyn *results, const char *path, CCUINT32 max_results)
{
    char **items = (char **)results->array;
    CCUINT32 count = results->size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (strcmp(items[i], path) == 0)
            return 0;
    }
    if (count >= max_results)
        return -1;
    return find_push_str(results, path) ? 1 : -2;
}

static int find_compare_str(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static CCBOOL find_match(
    const char *name,
    char **includes,
    CCUINT32 include_count,
    char **excludes,
    CCUINT32 exclude_count
)
{
    CCBOOL hit = CCFALSE;
    for (CCUINT32 i = 0; i < include_count; i++)
    {
        if (cc_wildcard(includes[i], name))
        {
            hit = CCTRUE;
            break;
        }
    }
    if (!hit)
        return CCFALSE;
    for (CCUINT32 i = 0; i < exclude_count; i++)
    {
        if (cc_wildcard(excludes[i], name))
            return CCFALSE;
    }
    return CCTRUE;
}

static CCINT32 find_list_callback(void *data, void *user)
{
    CCFsEntry *entry = (CCFsEntry *)data;
    CCMFindListCtx *ctx = (CCMFindListCtx *)user;
    CCMFindEntry item;
    memset(&item, 0, sizeof(item));
    size_t n = strlen(entry->name);
    item.name = ccalloc(
        NULL, (CCUINT64)n + 1,
        CC_DBG_REL_TAG(".ccm find 条目", NULL)
    );
    if (!item.name)
        return 1;
    memcpy(item.name, entry->name, n + 1);
    item.type = entry->type;
    if (cc_dyn_core_push(
            &ctx->entries, (CCUINT32)sizeof(CCMFindEntry),
            (CCINT32)sizeof(CCMFindEntry), &item
        ) != 0
    )
    {
        ccalloc(item.name, 0, NULL);
        return 1;
    }
    return 0;
}

static char *find_join_output(const char *dir, const char *rel)
{
    size_t dl = strlen(dir);
    while (dl > 1 && dir[dl - 1] == '/')
        dl--;
#ifdef CC_WINDOWS
    if (dl == 2 && dir[1] == ':')
        dl = 3;
#endif
    size_t rl = strlen(rel);
    CCBOOL has_slash = (dl > 0 && dir[dl - 1] == '/') ? CCTRUE : CCFALSE;
    size_t need = dl + (has_slash ? 0 : 1) + rl + 1;
    char *out = ccalloc(NULL, need, CC_DBG_REL_TAG(".ccm find 结果", NULL));
    if (!out)
        return NULL;
    memcpy(out, dir, dl);
    size_t pos = dl;
    if (!has_slash)
        out[pos++] = '/';
    memcpy(out + pos, rel, rl + 1);
    return out;
}

static void find_free_results(CCDyn *results)
{
    char **items = (char **)results->array;
    CCUINT32 count = results->size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
        ccalloc(items[i], 0, NULL);
    if (results->array)
        ccalloc(results->array, 0, NULL);
    memset(results, 0, sizeof(*results));
}

static void find_free_str_list(CCDyn *list)
{
    char **items = (char **)list->array;
    CCUINT32 count = list->size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
        ccalloc(items[i], 0, NULL);
    if (list->array)
        ccalloc(list->array, 0, NULL);
    memset(list, 0, sizeof(*list));
}

static void find_free_stack(CCDyn *stack)
{
    CCMFindStackItem *items = (CCMFindStackItem *)stack->array;
    CCUINT32 count = stack->size / (CCUINT32)sizeof(CCMFindStackItem);
    for (CCUINT32 i = count; i > 0; i--)
    {
        ccalloc(items[i - 1].vpath, 0, NULL);
        ccalloc(items[i - 1].rel, 0, NULL);
    }
    if (stack->array)
        ccalloc(stack->array, 0, NULL);
    memset(stack, 0, sizeof(*stack));
}

static void find_free_entries(CCMFindListCtx *lc)
{
    CCMFindEntry *entries = (CCMFindEntry *)lc->entries.array;
    CCUINT32 count = lc->entries.size / (CCUINT32)sizeof(CCMFindEntry);
    for (CCUINT32 i = 0; i < count; i++)
        ccalloc(entries[i].name, 0, NULL);
    if (lc->entries.array)
        ccalloc(lc->entries.array, 0, NULL);
    memset(lc, 0, sizeof(*lc));
}

typedef struct ccm_find_scan {
    CCFiles *fs;
    const CCMakeCCMLimits *limits;
    const char *dir_prefix;
    const char *file;
    CCUINT32 line;
    char **includes;
    CCUINT32 include_count;
    char **excludes;
    CCUINT32 exclude_count;
    CCDyn *results;
    CCDyn stack; /* CCMFindStackItem[] */
} CCMFindScan;

static void find_stack_item_free(CCMFindStackItem *item)
{
    ccalloc(item->vpath, 0, NULL);
    ccalloc(item->rel, 0, NULL);
    memset(item, 0, sizeof(*item));
}

static CCBOOL find_make_item(
    CCMFindStackItem *item,
    const char *vpath,
    const char *rel,
    CCUINT32 depth
)
{
    memset(item, 0, sizeof(*item));
    size_t vl = strlen(vpath);
    size_t rl = strlen(rel);
    item->vpath = ccalloc(
        NULL, (CCUINT64)vl + 1,
        CC_DBG_REL_TAG(".ccm find 虚拟路径", NULL)
    );
    if (!item->vpath)
        return CCFALSE;
    item->rel = ccalloc(
        NULL, (CCUINT64)rl + 1,
        CC_DBG_REL_TAG(".ccm find 相对路径", NULL)
    );
    if (!item->rel)
    {
        ccalloc(item->vpath, 0, NULL);
        item->vpath = NULL;
        return CCFALSE;
    }
    memcpy(item->vpath, vpath, vl + 1);
    memcpy(item->rel, rel, rl + 1);
    item->depth = depth;
    return CCTRUE;
}

static CCBOOL find_make_child_paths(
    const CCMFindStackItem *dir,
    const char *name,
    char **out_vpath,
    char **out_rel
)
{
    *out_vpath = NULL;
    *out_rel = NULL;
    size_t vl = strlen(dir->vpath);
    size_t rl = strlen(dir->rel);
    size_t nl = strlen(name);
    char *cv = ccalloc(
        NULL, (CCUINT64)vl + nl + 2,
        CC_DBG_REL_TAG(".ccm find 虚拟路径", NULL)
    );
    if (!cv)
        return CCFALSE;
    char *cr = ccalloc(
        NULL, (CCUINT64)rl + (rl ? 1 : 0) + nl + 1,
        CC_DBG_REL_TAG(".ccm find 相对路径", NULL)
    );
    if (!cr)
    {
        ccalloc(cv, 0, NULL);
        return CCFALSE;
    }
    memcpy(cv, dir->vpath, vl);
    cv[vl] = '/';
    memcpy(cv + vl + 1, name, nl + 1);
    if (rl)
    {
        memcpy(cr, dir->rel, rl);
        cr[rl] = '/';
        memcpy(cr + rl + 1, name, nl + 1);
    }
    else
    {
        memcpy(cr, name, nl + 1);
    }
    *out_vpath = cv;
    *out_rel = cr;
    return CCTRUE;
}

static CCBOOL find_scan_entry(
    CCMFindScan *scan,
    const CCMFindStackItem *dir,
    const CCMFindEntry *entry
)
{
    if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0)
        return CCTRUE;
    if (entry->type == CC_FS_TYPE_DIR &&
        dir->depth >= scan->limits->max_find_depth)
    {
        ccm_parse_failf(scan->file, scan->line, "${find} 目录深度超过限制");
        return CCFALSE;
    }

    char *cv = NULL;
    char *cr = NULL;
    if (!find_make_child_paths(dir, entry->name, &cv, &cr))
    {
        ccm_parse_failf(scan->file, scan->line, "内存分配失败");
        return CCFALSE;
    }

    if (entry->type == CC_FS_TYPE_DIR)
    {
        CCMFindStackItem child;
        memset(&child, 0, sizeof(child));
        child.vpath = cv;
        child.rel = cr;
        child.depth = dir->depth + 1;
        if (cc_dyn_core_push(
                &scan->stack, (CCUINT32)sizeof(CCMFindStackItem),
                (CCINT32)sizeof(CCMFindStackItem), &child
            ) != 0
        )
        {
            ccm_parse_failf(scan->file, scan->line, "内存分配失败");
            goto FailedPaths;
        }
        return CCTRUE;
    }

    if (!find_match(
            entry->name, scan->includes, scan->include_count,
            scan->excludes, scan->exclude_count
        ))
        goto Succeeded;

    if (scan->results->size / (CCUINT32)sizeof(char *) >=
        scan->limits->max_find_results)
    {
        ccm_parse_failf(scan->file, scan->line, "${find} 结果数量超过限制");
        goto FailedPaths;
    }
    char *joined = find_join_output(scan->dir_prefix, cr);
    if (!joined)
    {
        ccm_parse_failf(scan->file, scan->line, "内存分配失败");
        goto FailedPaths;
    }
    CCINT32 added = find_add_result(
        scan->results, joined, scan->limits->max_find_results
    );
    if (added != 0 && added != 1)
    {
        ccm_parse_failf(
            scan->file, scan->line,
            added == -1 ? "${find} 结果数量超过限制" : "内存分配失败"
        );
        goto FailedJoined;
    }
    ccalloc(joined, 0, NULL);

Succeeded:
    ccalloc(cr, 0, NULL);
    ccalloc(cv, 0, NULL);
    return CCTRUE;

FailedJoined:
    ccalloc(joined, 0, NULL);
FailedPaths:
    ccalloc(cr, 0, NULL);
    ccalloc(cv, 0, NULL);
    return CCFALSE;
}

static CCBOOL find_scan_entries(CCMFindScan *scan, const CCMFindStackItem *dir)
{
    CCMFindListCtx lc;
    memset(&lc, 0, sizeof(lc));

    if (ccfiles_list(scan->fs, dir->vpath, find_list_callback, &lc) != CC_FS_OK)
    {
        ccm_parse_failf(scan->file, scan->line, "${find} 目录遍历失败：%s", dir->vpath);
        find_free_entries(&lc);
        return CCFALSE;
    }

    CCMFindEntry *entries = (CCMFindEntry *)lc.entries.array;
    CCUINT32 count = lc.entries.size / (CCUINT32)sizeof(CCMFindEntry);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (!find_scan_entry(scan, dir, &entries[i]))
        {
            find_free_entries(&lc);
            return CCFALSE;
        }
    }
    find_free_entries(&lc);
    return CCTRUE;
}

static CCBOOL find_scan_loop(CCMFindScan *scan)
{
    CCMFindStackItem item;
    memset(&item, 0, sizeof(item));

    while (scan->stack.size >= (CCUINT32)sizeof(CCMFindStackItem))
    {
        if (cc_dyn_core_pop(
                &scan->stack, (CCUINT32)sizeof(CCMFindStackItem),
                (CCINT32)sizeof(CCMFindStackItem), &item
            ) != 0
        )
        {
            ccm_parse_failf(scan->file, scan->line, "内存分配失败");
            return CCFALSE;
        }
        if (!find_scan_entries(scan, &item))
        {
            find_stack_item_free(&item);
            return CCFALSE;
        }
        find_stack_item_free(&item);
    }
    return CCTRUE;
}

static CCBOOL find_scan_dir(CCMFindScan *scan, const char *vpath)
{
    CCMFindStackItem root;
    memset(&root, 0, sizeof(root));

    if (!find_make_item(&root, vpath, "", 0))
    {
        ccm_parse_failf(scan->file, scan->line, "内存分配失败");
        return CCFALSE;
    }
    if (cc_dyn_core_push(
            &scan->stack, (CCUINT32)sizeof(CCMFindStackItem),
            (CCINT32)sizeof(CCMFindStackItem), &root
        ) != 0
    )
    {
        ccm_parse_failf(scan->file, scan->line, "内存分配失败");
        find_stack_item_free(&root);
        return CCFALSE;
    }
    /* 所有权已转移给栈；root 本地副本不再持有路径 */
    root.vpath = NULL;
    root.rel = NULL;

    CCBOOL ok = find_scan_loop(scan);
    find_free_stack(&scan->stack);
    return ok;
}

static CCBOOL find_vpath_free(CCFiles *fs, const char *vpath)
{
    if (!fs || !vpath)
        return CCTRUE;
    /* 先卸载目录锚点，丢弃投影子节点；再摘除还原后的内存节点。
       直接对目录锚点递归删除会按锚点物理路径解析子节点，触及宿主文件。 */
    if (ccfiles_unmount(fs, vpath) == CC_FS_OK)
        (void)ccfiles_remove(fs, vpath);
    return CCTRUE;
}

static void find_probe_destroy(CCFiles **fs, CCDyn *mounted)
{
    if (!*fs)
        return;
    char **items = (char **)mounted->array;
    CCUINT32 count = mounted->size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < count; i++)
        (void)find_vpath_free(*fs, items[i]);
    ccfiles_destroy(*fs);
    *fs = NULL;
}

CCBOOL ccm_find_eval(
    CCMakeCCMContext *ctx,
    const char *args,
    CCUINT32 len,
    char **out,
    CCUINT32 *out_len,
    const char *file,
    CCUINT32 line
)
{
    if (!ctx || !args || !out || !out_len)
        return CCFALSE;
    *out = NULL;
    *out_len = 0;

    CCMFindTokens tokens;
    if (!find_tokenize(args, len, &tokens, ctx->limits.max_find_args, file, line))
        return CCFALSE;

    CCBOOL ok = CCFALSE;
    CCDyn dirs;
    CCDyn includes;
    CCDyn excludes;
    memset(&dirs, 0, sizeof(dirs));
    memset(&includes, 0, sizeof(includes));
    memset(&excludes, 0, sizeof(excludes));
    CCDyn results;
    memset(&results, 0, sizeof(results));
    CCDyn buf;
    memset(&buf, 0, sizeof(buf));
    CCFiles *fs = NULL;
    CCDyn mounted;
    memset(&mounted, 0, sizeof(mounted));

    CCUINT32 mode = 0; /* 0=目录，1=include，2=exclude */
    char **tk = (char **)tokens.tokens.array;
    CCUINT32 tc = tokens.tokens.size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = 0; i < tc; i++)
    {
        if (strcmp(tk[i], "-include") == 0)
        {
            mode = 1;
            continue;
        }
        if (strcmp(tk[i], "-exclude") == 0)
        {
            mode = 2;
            continue;
        }
        CCDyn *dst = (mode == 0) ? &dirs : (mode == 1 ? &includes : &excludes);
        if (!find_push_str(dst, tk[i]))
        {
            ccm_parse_failf(file, line, "内存分配失败");
            goto Finished;
        }
    }

    if (dirs.size == 0 || includes.size == 0)
    {
        ccm_parse_failf(file, line, "${find} 需要目录与 -include 通配符");
        goto Finished;
    }

    fs = ccfiles_create();
    if (!fs)
    {
        ccm_parse_failf(file, line, "VFS 探针创建失败");
        goto Finished;
    }

    char **dir_items = (char **)dirs.array;
    CCUINT32 dir_count = dirs.size / (CCUINT32)sizeof(char *);
    CCUINT32 serial = 0;

    for (CCUINT32 d = 0; d < dir_count; d++)
    {
        char *real = ccm_path_normalize(
            dir_items[d], ctx->work_dir,
            CC_DBG_REL_TAG(".ccm find 目录", NULL)
        );
        if (!real)
        {
            ccm_parse_failf(file, line, "${find} 目录路径非法：%s", dir_items[d]);
            goto Finished;
        }
        if (ccm_host_path_type(real) != CC_CCM_HOST_DIR)
        {
            ccm_parse_failf(
                file, line, "${find} 目录不存在或不是目录：%s",
                dir_items[d]
            );
            ccalloc(real, 0, NULL);
            goto Finished;
        }

        char vpath[32];
        CCFsStat st;
        for (;;)
        {
            snprintf(vpath, sizeof(vpath), "/__find:%u", serial++);
            if (ccfiles_stat(fs, vpath, &st) == CC_FS_ERR_NOENT)
                break;
        }
        CCINT32 mr = ccfiles_mount(fs, vpath, real, CC_FS_MOUNT_DIR);
        ccalloc(real, 0, NULL);
        if (mr != CC_FS_OK)
        {
            ccm_parse_failf(file, line, "${find} 目录挂载失败");
            goto Finished;
        }
        if (!find_push_str(&mounted, vpath))
        {
            (void)find_vpath_free(fs, vpath);
            ccm_parse_failf(file, line, "内存分配失败");
            goto Finished;
        }

        CCMFindScan scan;
        memset(&scan, 0, sizeof(scan));
        scan.fs = fs;
        scan.limits = &ctx->limits;
        scan.dir_prefix = dir_items[d];
        scan.file = file;
        scan.line = line;
        scan.includes = (char **)includes.array;
        scan.include_count = includes.size / (CCUINT32)sizeof(char *);
        scan.excludes = (char **)excludes.array;
        scan.exclude_count = excludes.size / (CCUINT32)sizeof(char *);
        scan.results = &results;
        if (!find_scan_dir(&scan, vpath))
            goto Finished;
    }

    {
        char **res = (char **)results.array;
        CCUINT32 rc = results.size / (CCUINT32)sizeof(char *);
        if (rc > 1)
            qsort(res, rc, sizeof(char *), find_compare_str);

        for (CCUINT32 i = 0; i < rc; i++)
        {
            if (i > 0)
            {
                char sp = ' ';
                if (cc_dyn_core_push(&buf, 1, 1, &sp) != 0)
                {
                    ccm_parse_failf(file, line, "内存分配失败");
                    goto Finished;
                }
            }
            if (!find_buf_put(&buf, res[i], (CCUINT32)strlen(res[i])))
            {
                ccm_parse_failf(file, line, "内存分配失败");
                goto Finished;
            }
        }
        char zero = '\0';
        if (cc_dyn_core_push(&buf, 1, 1, &zero) != 0)
        {
            ccm_parse_failf(file, line, "内存分配失败");
            goto Finished;
        }
        *out = (char *)buf.array;
        *out_len = buf.size - 1;
        memset(&buf, 0, sizeof(buf));
        ok = CCTRUE;
    }

Finished:
    if (buf.array)
        ccalloc(buf.array, 0, NULL);
    find_free_results(&results);
    find_probe_destroy(&fs, &mounted);
    find_free_str_list(&mounted);
    find_free_str_list(&excludes);
    find_free_str_list(&includes);
    find_free_str_list(&dirs);
    find_tokens_free(&tokens);
    return ok;
}
