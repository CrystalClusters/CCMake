/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .ccm 第二阶段内建函数：${ref} / ${find}
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccm_internal.h"
#include "ccm_scan_util.h"
#include "ccvar.h"

#include <stdio.h>
#include <string.h>

typedef struct ccm_buf {
    CCDyn dyn;
} CCMBuf;

static CCBOOL buf_put(CCMBuf *b, const char *text, CCUINT32 len)
{
    for (CCUINT32 i = 0; i < len; i++)
    {
        if (cc_dyn_core_push(&b->dyn, 1, 1, (void *)&text[i]) != 0)
            return CCFALSE;
    }
    return CCTRUE;
}

static CCBOOL buf_puts(CCMBuf *b, const char *text)
{
    return buf_put(b, text, (CCUINT32)strlen(text));
}

static char *buf_finish(CCMBuf *b, CCUINT32 *out_len)
{
    char zero = '\0';
    if (cc_dyn_core_push(&b->dyn, 1, 1, &zero) != 0)
    {
        if (b->dyn.array)
            ccalloc(b->dyn.array, 0, NULL);
        memset(&b->dyn, 0, sizeof(b->dyn));
        return NULL;
    }
    if (out_len)
        *out_len = b->dyn.size - 1;
    return (char *)b->dyn.array;
}

static void buf_free(CCMBuf *b)
{
    if (b->dyn.array)
        ccalloc(b->dyn.array, 0, NULL);
    memset(&b->dyn, 0, sizeof(b->dyn));
}

typedef struct ccm_serialize_ctx {
    CCVarTree *tree;
    CCMBuf *buf;
    const char *file;
    CCUINT32 line;
    CCBOOL ok;
    CCBOOL first;
} CCMSerializeCtx;

static CCBOOL ccm_serialize_value(
    CCVarTree *tree,
    const CCVarValue *value,
    CCMBuf *buf,
    const char *file,
    CCUINT32 line
);

static CCINT32 ccm_serialize_elem(
    CCUINT32 index,
    const CCVarValue *value,
    void *user
)
{
    CC_UNUSED(index);
    CCMSerializeCtx *ctx = (CCMSerializeCtx *)user;
    if (!ctx->ok)
        return 1;
    if (value->type == CC_VAR_OBJECT || value->type == CC_VAR_ARRAY)
    {
        ctx->ok = ccm_parse_failf(
            ctx->file, ctx->line,
            "数组内嵌对象/数组无法序列化为字符串"
        );
        return 1;
    }
    if (!ctx->first && !buf_puts(ctx->buf, " "))
    {
        ctx->ok = ccm_parse_failf(ctx->file, ctx->line, "内存分配失败");
        return 1;
    }
    ctx->first = CCFALSE;
    ctx->ok = ccm_serialize_value(
        ctx->tree, value, ctx->buf,
        ctx->file, ctx->line
    );
    return ctx->ok ? 0 : 1;
}

static CCBOOL ccm_serialize_value(
    CCVarTree *tree,
    const CCVarValue *value,
    CCMBuf *buf,
    const char *file,
    CCUINT32 line
)
{
    char num[64];
    switch (value->type)
    {
    case CC_VAR_STRING:
        return buf_put(buf, value->as.string.data, value->as.string.len);
    case CC_VAR_INT:
        snprintf(num, sizeof(num), "%lld", (long long)value->as.integer);
        return buf_puts(buf, num);
    case CC_VAR_FLOAT:
        snprintf(num, sizeof(num), "%.17g", value->as.floating);
        return buf_puts(buf, num);
    case CC_VAR_BOOL:
        return buf_puts(buf, value->as.boolean ? "true" : "false");
    case CC_VAR_ARRAY:
    {
        /* 数组元素通过路径逐项读取：调用方已展开数组值时无法直接拿到子元素，
           这里由 ccvar_foreach_element 通过数组路径完成。 */
        CC_UNUSED(tree);
        return ccm_parse_failf(file, line, "内部错误：数组值缺少路径");
    }
    case CC_VAR_OBJECT:
        return ccm_parse_failf(file, line, "对象不能作为字符串引用");
    default:
        return ccm_parse_failf(file, line, "无效值类型");
    }
}

/* ${ref} 的结果可能来自数组；通过路径重新遍历并序列化 */
static CCBOOL ccm_serialize_path(
    CCVarTree *tree,
    const char *path,
    CCMBuf *buf,
    const char *file,
    CCUINT32 line
)
{
    CCVarValue value;
    CCVarResult r = ccvar_get(tree, path, &value);
    if (r != CC_VAR_OK)
    {
        if (r == CC_VAR_ERR_NOENT)
            return ccm_parse_failf(file, line, "引用的字段不存在：%s", path);
        return ccm_parse_failf(file, line, "无法引用字段：%s", path);
    }

    if (value.type != CC_VAR_ARRAY)
        return ccm_serialize_value(tree, &value, buf, file, line);

    CCMSerializeCtx ctx;
    ctx.tree = tree;
    ctx.buf = buf;
    ctx.file = file;
    ctx.line = line;
    ctx.ok = CCTRUE;
    ctx.first = CCTRUE;
    CCINT32 rc = ccvar_foreach_element(tree, path, ccm_serialize_elem, &ctx);
    if (rc != 0 || !ctx.ok)
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL ccm_expand_one(
    CCMakeCCMContext *ctx,
    const char *raw,
    CCUINT32 begin,
    CCUINT32 end,
    CCVarTree *tree,
    CCMBuf *out,
    const char *file,
    CCUINT32 line
)
{
    char *inner = NULL;
    CCUINT32 inner_len = 0;
    if (!ccm_builtin_eval(
            ctx, raw + begin, end - begin, tree,
            &inner, &inner_len, file, line
        )
    )
        return CCFALSE;
    CCBOOL ok = buf_put(out, inner, inner_len);
    if (inner)
        ccalloc(inner, 0, NULL);
    if (!ok)
        return ccm_parse_failf(file, line, "内存分配失败");
    return CCTRUE;
}

/* 展开参数中的嵌套 ${...}，供 ${find} 使用；引号内的 ${...} 同样展开 */
static CCBOOL ccm_expand_builtins(
    CCMakeCCMContext *ctx,
    const char *raw,
    CCUINT32 len,
    CCVarTree *tree,
    CCMBuf *out,
    const char *file,
    CCUINT32 line
)
{
    CCUINT32 i = 0;
    while (i < len)
    {
        if (raw[i] == '\0')
            return ccm_parse_failf(file, line, "内建函数参数包含 NUL 字节");
        if (raw[i] == '"')
        {
            if (!buf_put(out, &raw[i], 1))
                return ccm_parse_failf(file, line, "内存分配失败");
            i++;
            while (i < len && raw[i] != '"')
            {
                if (raw[i] == '\0')
                    return ccm_parse_failf(file, line, "内建函数参数包含 NUL 字节");
                if (raw[i] == '$' && i + 1 < len && raw[i + 1] == '{')
                {
                    CCUINT64 end64 = 0;
                    if (!ccm_scan_builtin_end(raw, len, i + 1, &end64))
                        return ccm_parse_failf(file, line, "内建函数花括号未闭合");
                    CCUINT32 end = (CCUINT32)end64;
                    if (!ccm_expand_one(
                            ctx, raw, i + 2, end, tree,
                            out, file, line
                        ))
                        return CCFALSE;
                    i = end + 1;
                    continue;
                }
                if (!buf_put(out, &raw[i], 1))
                    return ccm_parse_failf(file, line, "内存分配失败");
                i++;
            }
            if (i < len)
            {
                if (!buf_put(out, &raw[i], 1))
                    return ccm_parse_failf(file, line, "内存分配失败");
                i++;
            }
            continue;
        }
        if (raw[i] == '$' && i + 1 < len && raw[i + 1] == '{')
        {
            CCUINT64 end64 = 0;
            if (!ccm_scan_builtin_end(raw, len, i + 1, &end64))
                return ccm_parse_failf(file, line, "内建函数花括号未闭合");
            CCUINT32 end = (CCUINT32)end64;
            if (!ccm_expand_one(
                    ctx, raw, i + 2, end, tree,
                    out, file, line
                ))
                return CCFALSE;
            i = end + 1;
            continue;
        }
        if (!buf_put(out, &raw[i], 1))
            return ccm_parse_failf(file, line, "内存分配失败");
        i++;
    }
    return CCTRUE;
}

static const char *trim_begin(const char *text, CCUINT32 len, CCUINT32 *start)
{
    CCUINT32 i = 0;
    while (i < len && (text[i] == ' ' || text[i] == '\t' ||
                       text[i] == '\r' || text[i] == '\n'))
        i++;
    *start = i;
    return text + i;
}

CCBOOL ccm_builtin_eval(
    CCMakeCCMContext *ctx,
    const char *raw,
    CCUINT32 len,
    CCVarTree *tree,
    char **out,
    CCUINT32 *out_len,
    const char *file,
    CCUINT32 line
)
{
    if (!raw || !tree || !out || !out_len)
        return CCFALSE;
    *out = NULL;
    *out_len = 0;

    CCUINT32 pos = 0;
    trim_begin(raw, len, &pos);
    CCUINT32 name_begin = pos;
    while (pos < len &&
           ((raw[pos] >= 'a' && raw[pos] <= 'z') ||
            (raw[pos] >= 'A' && raw[pos] <= 'Z') ||
            (raw[pos] >= '0' && raw[pos] <= '9') || raw[pos] == '_'))
        pos++;
    CCUINT32 name_len = pos - name_begin;
    if (name_len == 0)
        return ccm_parse_failf(file, line, "内建函数缺少函数名");

    if (name_len == 3 && memcmp(raw + name_begin, "ref", 3) == 0)
    {
        CCUINT32 p = pos;
        while (p < len && (raw[p] == ' ' || raw[p] == '\t'))
            p++;
        CCUINT32 end = len;
        while (end > p && (raw[end - 1] == ' ' || raw[end - 1] == '\t' ||
                           raw[end - 1] == '\r' || raw[end - 1] == '\n'))
            end--;
        if (end == p)
            return ccm_parse_failf(file, line, "${ref} 缺少字段名");

        char *path = ccalloc(
            NULL, (CCUINT64)(end - p) + 1,
            CC_DBG_REL_TAG(".ccm 引用路径", NULL)
        );
        if (!path)
            return ccm_parse_failf(file, line, "内存分配失败");
        memcpy(path, raw + p, end - p);
        path[end - p] = '\0';

        CCMBuf buf;
        memset(&buf, 0, sizeof(buf));
        CCBOOL ok = ccm_serialize_path(tree, path, &buf, file, line);
        ccalloc(path, 0, NULL);
        if (!ok)
        {
            buf_free(&buf);
            return CCFALSE;
        }
        *out = buf_finish(&buf, out_len);
        if (!*out)
            return ccm_parse_failf(file, line, "内存分配失败");
        return CCTRUE;
    }

    if (name_len == 4 && memcmp(raw + name_begin, "find", 4) == 0)
    {
        CCMBuf expanded;
        memset(&expanded, 0, sizeof(expanded));
        if (!ccm_expand_builtins(
                ctx, raw + pos, len - pos, tree,
                &expanded, file, line
            )
        )
        {
            buf_free(&expanded);
            return CCFALSE;
        }
        char *args = buf_finish(&expanded, NULL);
        if (!args)
            return ccm_parse_failf(file, line, "内存分配失败");
        CCBOOL ok = ccm_find_eval(
            ctx, args, (CCUINT32)strlen(args),
            out, out_len, file, line
        );
        ccalloc(args, 0, NULL);
        return ok;
    }

    return ccm_parse_failf(file, line, "未知的内建函数");
}
