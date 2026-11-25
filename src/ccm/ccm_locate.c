/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .ccm 文件检查与定位：VFS 装载、include 闭包遍历、解析顺序生成
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccm_internal.h"
#include "ccm_safe.h"


#include "ccmake_console.h"

#include <stdio.h>

#define CC_CCM_LOCATE_COMMENT CC_DBG_REL_TAG("CCM 定位", NULL)

/* DFS 帧：当前文件与下一条待处理 include 边 */
typedef struct ccmake_ccm_frame_struct {
    CCUINT32 file;
    CCUINT32 next;
} CCMakeCCMFrame;

static CCMakeCCMFile *file_at(CCMakeCCMContext *ctx, CCUINT32 index)
{
    return &((CCMakeCCMFile *)ctx->files.array)[index];
}

static CCMakeCCMInclude *include_at(CCMakeCCMContext *ctx, CCUINT32 index)
{
    return &((CCMakeCCMInclude *)ctx->includes.array)[index];
}

static CCUINT32 file_count(const CCMakeCCMContext *ctx)
{
    return ctx->files.size / (CCUINT32)sizeof(CCMakeCCMFile);
}

static CCUINT32 include_count(const CCMakeCCMContext *ctx)
{
    return ctx->includes.size / (CCUINT32)sizeof(CCMakeCCMInclude);
}

void ccm_context_init(CCMakeCCMContext *ctx)
{
    if (!ctx)
        return;
    memset(ctx, 0, sizeof(*ctx));
    // 0 表示动态/不设硬上限，由实际资源与表示边界约束
    ctx->limits.max_total_bytes = 0;
    ctx->limits.max_file_bytes = 0;
    ctx->limits.max_files = 0;
    ctx->limits.max_include_edges = 0;
    ctx->limits.max_include_depth = 0;
    ctx->limits.max_token_bytes = 0;
    ctx->limits.max_array_elements = 0;
    ctx->limits.max_object_members = 0;
    ctx->limits.max_value_depth = 0;
    ctx->limits.max_find_args = 0;
    ctx->limits.max_find_results = 0;
    ctx->limits.max_find_depth = 0;
}

static void ccm_release_vfs(CCMakeCCMContext *ctx)
{
    if (!ctx || !ctx->fs)
        return;

    CCUINT32 count = file_count(ctx);
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCMakeCCMFile *f = file_at(ctx, i);
        if (f->mounted && f->v_path)
        {
            /* 显式挂载锚点只摘虚拟节点，不删除宿主文件 */
            (void)ccfiles_remove(ctx->fs, f->v_path);
            f->mounted = CCFALSE;
        }
    }
    ccfiles_destroy(ctx->fs);
    ctx->fs = NULL;
}

void ccm_context_clear(CCMakeCCMContext *ctx)
{
    if (!ctx)
        return;

    ccm_release_vfs(ctx);

    /* 依赖序：VFS 先释放；其余按创建顺序逆序 */
    if (ctx->order.array)
        ccalloc(ctx->order.array, 0, NULL);
    if (ctx->stack.array)
        ccalloc(ctx->stack.array, 0, NULL);

    CCUINT32 includes = include_count(ctx);
    for (CCUINT32 i = includes; i > 0; i--)
    {
        CCMakeCCMInclude *inc = include_at(ctx, i - 1);
        if (inc->literal)
            ccalloc(inc->literal, 0, NULL);
    }
    if (ctx->includes.array)
        ccalloc(ctx->includes.array, 0, NULL);

    CCUINT32 files = file_count(ctx);
    for (CCUINT32 i = files; i > 0; i--)
    {
        CCMakeCCMFile *f = file_at(ctx, i - 1);
        if (f->real_path)
            ccalloc(f->real_path, 0, NULL);
        if (f->v_path)
            ccalloc(f->v_path, 0, NULL);
        if (f->content)
            ccalloc(f->content, 0, NULL);
    }
    if (ctx->files.array)
        ccalloc(ctx->files.array, 0, NULL);

    if (ctx->work_dir)
        ccalloc(ctx->work_dir, 0, NULL);

    memset(ctx, 0, sizeof(*ctx));
}

/* 追加文件项；函数复制 real_path，调用方保留其所有权 */
static CCUINT32 ccm_append_file(
    CCMakeCCMContext *ctx,
    const char *real_path,
    CCUINT32 parent,
    CCUINT32 via_line,
    CCUINT32 depth
)
{
    if (!ctx || !real_path)
        return CC_CCM_NIL;
    if (ctx->limits.max_files != 0 &&
        file_count(ctx) >= ctx->limits.max_files)
        return CC_CCM_NIL;

    size_t path_len = strlen(real_path);
    char *path_copy = ccalloc(
        NULL, (CCUINT64)path_len + 1,
        CC_DBG_REL_TAG(".ccm 文件路径", NULL)
    );
    if (!path_copy)
        return CC_CCM_NIL;
    memcpy(path_copy, real_path, path_len + 1);

    CCMakeCCMFile item;
    memset(&item, 0, sizeof(item));
    item.real_path = path_copy;
    item.v_path = NULL;
    item.content = NULL;
    item.size = 0;
    item.include_first = CC_CCM_NIL;
    item.include_count = 0;
    item.parent = parent;
    item.via_line = via_line;
    item.depth = depth;
    item.state = CC_CCM_FILE_UNSEEN;
    item.mounted = CCFALSE;

    if (!ccm_dyn_push(
            &ctx->files,
            (CCUINT32)sizeof(CCMakeCCMFile),
            (CCINT32)sizeof(CCMakeCCMFile),
            &item
        ))
    {
        ccalloc(path_copy, 0, NULL);
        return CC_CCM_NIL;
    }
    return file_count(ctx) - 1;
}

static CCUINT32 ccm_find_file(CCMakeCCMContext *ctx, const char *real_path)
{
    CCUINT32 count = file_count(ctx);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (ccm_path_equal(file_at(ctx, i)->real_path, real_path))
            return i;
    }
    return CC_CCM_NIL;
}

static CCBOOL ccm_stack_push(CCMakeCCMContext *ctx, CCUINT32 file)
{
    CCMakeCCMFrame frame;
    frame.file = file;
    frame.next = 0;
    return ccm_dyn_push(
            &ctx->stack,
            (CCUINT32)sizeof(CCMakeCCMFrame),
            (CCINT32)sizeof(CCMakeCCMFrame),
            &frame
        ) ? CCTRUE : CCFALSE;
}

static CCBOOL ccm_stack_top(CCMakeCCMContext *ctx, CCMakeCCMFrame *out)
{
    CCUINT32 count = ctx->stack.size / (CCUINT32)sizeof(CCMakeCCMFrame);
    if (count == 0)
        return CCFALSE;
    return (cc_dyn_core_seek(
            &ctx->stack,
            (CCUINT32)sizeof(CCMakeCCMFrame),
            (CCINT32)sizeof(CCMakeCCMFrame),
            out, count - 1
        ) == 0
    ) ? CCTRUE : CCFALSE;
}

static CCBOOL ccm_stack_set_top(CCMakeCCMContext *ctx, const CCMakeCCMFrame *frame)
{
    CCUINT32 count = ctx->stack.size / (CCUINT32)sizeof(CCMakeCCMFrame);
    if (count == 0)
        return CCFALSE;
    return (cc_dyn_core_set(
            &ctx->stack,
            (CCUINT32)sizeof(CCMakeCCMFrame),
            (CCINT32)sizeof(CCMakeCCMFrame),
            (void *)frame, count - 1
        ) == 0
    ) ? CCTRUE : CCFALSE;
}

static void ccm_stack_pop(CCMakeCCMContext *ctx)
{
    if (ctx->stack.size >= (CCUINT32)sizeof(CCMakeCCMFrame))
        ctx->stack.size -= (CCUINT32)sizeof(CCMakeCCMFrame);
}

/* 在虚拟根下找一个尚未被占用的合成挂载点 */
static char *ccm_make_vpath(CCMakeCCMContext *ctx)
{
    for (CCUINT32 serial = 0; serial < 0x00FFFFFFu; serial++)
    {
        char name[32];
        CCFsStat st;
        snprintf(name, sizeof(name), "/__ccm:%u", serial);
        CCINT32 ret = ccfiles_stat(ctx->fs, name, &st);
        if (ret == CC_FS_ERR_NOENT)
        {
            size_t len = strlen(name);
            char *out = ccalloc(NULL, (CCUINT64)len + 1, CC_CCM_LOCATE_COMMENT);
            if (!out)
                return NULL;
            memcpy(out, name, len + 1);
            return out;
        }
        if (ret != CC_FS_OK)
            return NULL;
    }
    return NULL;
}

static const char *ccm_fs_error_text(CCINT32 code)
{
    switch (code)
    {
    case CC_FS_ERR_NOENT:    return "文件不存在";
    case CC_FS_ERR_NOTDIR:   return "路径中的目录不存在";
    case CC_FS_ERR_ISDIR:    return "目标是目录，不是文件";
    case CC_FS_ERR_PERM:     return "权限不足";
    case CC_FS_ERR_NOSPACE:  return "内存不足";
    case CC_FS_ERR_IO:       return "无法读取";
    case CC_FS_ERR_PARAM:    return "路径非法";
    default:                 return "文件访问失败";
    }
}

static CCBOOL ccm_load_error(
    const char *real_path,
    const char *via_path,
    CCUINT32 via_line,
    const char *reason
)
{
    if (via_line > 0 && via_path)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：%s:%u: 无法导入 \"%s\"：%s\n",
            via_path, via_line, real_path, reason
        );
    }
    else
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：无法读取配置文件 \"%s\"：%s\n",
            real_path, reason
        );
    }
    return CCFALSE;
}

static CCBOOL ccm_strip_bom(
    char *content,
    CCUINT64 *size,
    const char *real_path,
    const char *via_path,
    CCUINT32 via_line
)
{
    if (*size >= 4 &&
        (((unsigned char)content[0] == 0xFF && (unsigned char)content[1] == 0xFE &&
          content[2] == '\0' && content[3] == '\0') ||
         (content[0] == '\0' && content[1] == '\0' &&
          (unsigned char)content[2] == 0xFE && (unsigned char)content[3] == 0xFF)))
        return ccm_load_error(
            real_path, via_path, via_line,
            "不支持 UTF-32 编码，请使用 ASCII/UTF-8"
        );

    if (*size >= 2 &&
        (((unsigned char)content[0] == 0xFF && (unsigned char)content[1] == 0xFE) ||
         ((unsigned char)content[0] == 0xFE && (unsigned char)content[1] == 0xFF)))
        return ccm_load_error(
            real_path, via_path, via_line,
            "不支持 UTF-16 编码，请使用 ASCII/UTF-8"
        );

    if (*size >= 3 &&
        (unsigned char)content[0] == 0xEF && (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF)
    {
        memmove(content, content + 3, (size_t)(*size - 3));
        *size -= 3;
        content[*size] = '\0';
    }
    return CCTRUE;
}

/*
 * 装载一个已登记文件：类型门、VFS 挂载、读取内容、扫描 include。
 * via_path/via_line 非空时，错误按“被哪个文件的哪一行导入”输出。
 */
static CCBOOL ccm_load_file(
    CCMakeCCMContext *ctx,
    CCUINT32 index,
    const char *via_path,
    CCUINT32 via_line
)
{
    CCMakeCCMFile *f = file_at(ctx, index);
    char *real_path = f->real_path;

    CC_CCM_HOST_TYPE type = ccm_host_path_type(real_path);
    if (type != CC_CCM_HOST_FILE)
    {
        const char *reason = "文件不存在";
        if (type == CC_CCM_HOST_DIR)
            reason = "目标是目录，不是文件";
        else if (type == CC_CCM_HOST_OTHER)
            reason = "不是普通文件";
        else if (type == CC_CCM_HOST_DENIED)
            reason = "权限不足";
        return ccm_load_error(real_path, via_path, via_line, reason);
    }

    if (!ctx->fs)
    {
        ctx->fs = ccfiles_create();
        if (!ctx->fs)
            return ccm_load_error(real_path, via_path, via_line, "VFS 探针创建失败");
    }

    f = file_at(ctx, index);
    f->v_path = ccm_make_vpath(ctx);
    if (!f->v_path)
        return ccm_load_error(real_path, via_path, via_line, "VFS 挂载点分配失败");

    CCINT32 mount_ret = ccfiles_mount(
        ctx->fs, f->v_path, real_path, CC_FS_MOUNT_FILE
    );
    if (mount_ret != CC_FS_OK)
        return ccm_load_error(real_path, via_path, via_line, ccm_fs_error_text(mount_ret));

    f = file_at(ctx, index);
    f->mounted = CCTRUE;

    CCFsStat st;
    if (ccfiles_stat(ctx->fs, f->v_path, &st) != CC_FS_OK ||
        st.type != CC_FS_TYPE_FILE)
        return ccm_load_error(real_path, via_path, via_line, "无法读取文件元信息");

    f = file_at(ctx, index);
    f->size = st.size;

    if (f->size == (CCUINT64)-1 ||
        f->size > (CCUINT64)(~(size_t)0) - 1)
        return ccm_load_error(real_path, via_path, via_line, "文件过大");
    if (ctx->limits.max_file_bytes != 0 &&
        f->size > ctx->limits.max_file_bytes)
        return ccm_load_error(real_path, via_path, via_line, "单个文件超过大小限制");
    if (ctx->limits.max_total_bytes != 0 &&
        (f->size > ctx->limits.max_total_bytes ||
         ctx->total_bytes > ctx->limits.max_total_bytes - f->size))
        return ccm_load_error(real_path, via_path, via_line, "配置文件总大小超过限制");

    f->content = ccalloc(
        NULL, f->size + 1,
        CC_DBG_REL_TAG(".ccm 文件内容", NULL)
    );
    if (!f->content)
        return ccm_load_error(real_path, via_path, via_line, "内存分配失败");
    f->content[0] = '\0';

    if (f->size > 0)
    {
        if (ccfiles_open(ctx->fs, f->v_path) != CC_FS_OK)
            return ccm_load_error(real_path, via_path, via_line, "无法打开文件");

        CCINT64 got = ccfiles_read(ctx->fs, f->content, 0, f->size);
        if (got < 0)
            return ccm_load_error(real_path, via_path, via_line, ccm_fs_error_text((CCINT32)got));
        if ((CCUINT64)got != f->size)
            return ccm_load_error(real_path, via_path, via_line, "读取不完整");
        f->content[f->size] = '\0';
    }

    f = file_at(ctx, index);
    if (!ccm_strip_bom(f->content, &f->size, f->real_path, via_path, via_line))
        return CCFALSE;

    CCUINT32 first = include_count(ctx);
    if (!ccm_scan_includes(
            f->content, f->size, f->real_path, &ctx->includes,
            ctx->limits.max_include_edges
        )
    )
        return CCFALSE;

    f = file_at(ctx, index);
    f->include_first = first;
    f->include_count = include_count(ctx) - first;
    ctx->total_bytes += f->size;
    return CCTRUE;
}

/* 打印循环导入链：file_node -> ... -> current -> file_node */
static CCBOOL ccm_report_cycle(
    CCMakeCCMContext *ctx,
    CCUINT32 current,
    CCUINT32 file_node,
    CCUINT32 edge_line
)
{
    CCDyn chain;
    memset(&chain, 0, sizeof(chain));

    CCUINT32 cur = current;
    CCBOOL reached = CCFALSE;
    while (cur != CC_CCM_NIL && cur < file_count(ctx))
    {
        if (!ccm_dyn_push(
                &chain, (CCUINT32)sizeof(CCUINT32),
                (CCINT32)sizeof(CCUINT32), &cur
            ))
            break;
        if (cur == file_node)
        {
            reached = CCTRUE;
            break;
        }
        cur = file_at(ctx, cur)->parent;
    }

    const char *current_path = file_at(ctx, current)->real_path;
    color_print(
        CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
        "配置文件定位失败：%s:%u: 检测到循环导入：",
        current_path, edge_line
    );

    if (!reached)
    {
        color_print("%s -> %s\n", file_at(ctx, file_node)->real_path, current_path);
        goto Done;
    }

    CCUINT32 count = chain.size / (CCUINT32)sizeof(CCUINT32);
    CCUINT32 *nodes = (CCUINT32 *)chain.array;
    color_print("%s", file_at(ctx, file_node)->real_path);
    for (CCINT32 i = (CCINT32)count - 2; i >= 0; i--)
    {
        CCUINT32 node = nodes[i];
        color_print(":%u -> %s", file_at(ctx, node)->via_line, file_at(ctx, node)->real_path);
    }
    color_print(":%u -> %s\n", edge_line, file_at(ctx, file_node)->real_path);

Done:
    if (chain.array)
        ccalloc(chain.array, 0, NULL);
    return CCFALSE;
}

/* 处理一条 include 边：解析路径、查重、必要时装载并压栈 */
static CCBOOL ccm_locate_include(
    CCMakeCCMContext *ctx,
    CCUINT32 parent_index,
    CCUINT32 edge_index
)
{
    CCMakeCCMFile *parent = file_at(ctx, parent_index);
    CCMakeCCMInclude *edge = include_at(ctx, edge_index);
    const char *parent_path = parent->real_path;
    CCUINT32 line = edge->line;
    const char *literal = edge->literal;

    char *dir = ccm_path_dirname(parent_path, CC_CCM_LOCATE_COMMENT);
    if (!dir)
        return ccm_load_error(literal, parent_path, line, "无法确定当前文件目录");

    char *resolved = ccm_path_normalize(literal, dir, CC_CCM_LOCATE_COMMENT);
    ccalloc(dir, 0, NULL);
    if (!resolved)
        return ccm_load_error(literal, parent_path, line, "@include 路径非法");

    edge = include_at(ctx, edge_index);

    CCUINT32 existing = ccm_find_file(ctx, resolved);
    if (existing != CC_CCM_NIL)
    {
        ccalloc(resolved, 0, NULL);
        edge = include_at(ctx, edge_index);
        edge->child = existing;
        if (file_at(ctx, existing)->state == CC_CCM_FILE_VISITING)
            return ccm_report_cycle(ctx, parent_index, existing, line);
        return CCTRUE;
    }

    CCUINT32 depth = file_at(ctx, parent_index)->depth + 1;
    if (ctx->limits.max_include_depth != 0 &&
        depth > ctx->limits.max_include_depth)
    {
        ccalloc(resolved, 0, NULL);
        return ccm_load_error(literal, parent_path, line, "include 深度超过限制");
    }
    if (ctx->limits.max_files != 0 &&
        file_count(ctx) >= ctx->limits.max_files)
    {
        ccalloc(resolved, 0, NULL);
        return ccm_load_error(literal, parent_path, line, "include 文件数量超过限制");
    }
    CCUINT32 index = ccm_append_file(ctx, resolved, parent_index, line, depth);
    ccalloc(resolved, 0, NULL);
    if (index == CC_CCM_NIL)
        return ccm_load_error(literal, parent_path, line, "内存分配失败");

    if (!ccm_load_file(ctx, index, parent_path, line))
        return CCFALSE;

    file_at(ctx, index)->state = CC_CCM_FILE_VISITING;
    include_at(ctx, edge_index)->child = index;
    if (!ccm_stack_push(ctx, index))
        return ccm_load_error(literal, parent_path, line, "内存分配失败");
    return CCTRUE;
}

static CCBOOL ccm_verify_result(CCMakeCCMContext *ctx)
{
    CCUINT32 files = file_count(ctx);
    CCUINT32 order = ctx->order.size / (CCUINT32)sizeof(CCUINT32);

    if (files == 0 || order != files)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：内部错误（文件数 %u，解析顺序 %u）\n",
            files, order
        );
        return CCFALSE;
    }

    for (CCUINT32 i = 0; i < files; i++)
    {
        if (file_at(ctx, i)->state != CC_CCM_FILE_VISITED)
        {
            color_print(
                CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
                "配置文件定位失败：内部错误（存在未完成文件）\n"
            );
            return CCFALSE;
        }
    }

    CCUINT32 last = 0;
    if (cc_dyn_core_seek(
            &ctx->order,
            (CCUINT32)sizeof(CCUINT32),
            (CCINT32)sizeof(CCUINT32),
            &last, order - 1
        ) != 0 ||
        last != ctx->root_index
    )
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：内部错误（根文件不在解析顺序末尾）\n"
        );
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL ccm_locate(const char *root_path, const char *work_dir, CCMakeCCMContext *ctx)
{
    if (!ctx)
        return CCFALSE;
    if (!root_path || !root_path[0])
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：缺少 -c/--config\n"
        );
        return CCFALSE;
    }

    CCBOOL ok = CCFALSE;
    char *cwd = ccm_path_cwd(CC_CCM_LOCATE_COMMENT);
    if (!cwd)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：无法获取当前工作目录\n"
        );
        return CCFALSE;
    }

    if (work_dir && work_dir[0])
    {
        ctx->work_dir = ccm_path_normalize(work_dir, cwd, CC_CCM_LOCATE_COMMENT);
        ccalloc(cwd, 0, NULL);
        cwd = NULL;
        if (!ctx->work_dir)
        {
            color_print(
                CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
                "配置文件定位失败：工作目录路径非法\n"
            );
            return CCFALSE;
        }
    }
    else
    {
        ctx->work_dir = cwd;
        cwd = NULL;
    }

    if (ccm_host_path_type(ctx->work_dir) != CC_CCM_HOST_DIR)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：工作目录不存在或不是目录：%s\n",
            ctx->work_dir
        );
        return CCFALSE;
    }

    char *root_norm = ccm_path_normalize(
        root_path, ctx->work_dir, CC_CCM_LOCATE_COMMENT
    );
    if (!root_norm)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：根配置文件路径非法\n"
        );
        return CCFALSE;
    }

    if (ctx->limits.max_files != 0 &&
        file_count(ctx) >= ctx->limits.max_files)
    {
        ccalloc(root_norm, 0, NULL);
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：文件数量超过限制\n"
        );
        return CCFALSE;
    }

    CCUINT32 root_index = ccm_append_file(
        ctx, root_norm, CC_CCM_NIL, 0, 0
    );
    ccalloc(root_norm, 0, NULL);
    if (root_index == CC_CCM_NIL)
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：内存分配失败\n"
        );
        return CCFALSE;
    }
    ctx->root_index = root_index;

    if (!ccm_load_file(ctx, ctx->root_index, NULL, 0))
        goto Finished;
    file_at(ctx, ctx->root_index)->state = CC_CCM_FILE_VISITING;
    if (!ccm_stack_push(ctx, ctx->root_index))
    {
        color_print(
            CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
            "配置文件定位失败：内存分配失败\n"
        );
        goto Finished;
    }

    while (ctx->stack.size >= (CCUINT32)sizeof(CCMakeCCMFrame))
    {
        CCMakeCCMFrame frame;
        if (!ccm_stack_top(ctx, &frame))
            goto Finished;

        CCMakeCCMFile *current = file_at(ctx, frame.file);
        if (frame.next < current->include_count)
        {
            CCUINT32 edge_index = current->include_first + frame.next;
            frame.next++;
            if (!ccm_stack_set_top(ctx, &frame))
            {
                color_print(
                    CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
                    "配置文件定位失败：内存分配失败\n"
                );
                goto Finished;
            }
            if (!ccm_locate_include(ctx, frame.file, edge_index))
                goto Finished;
            continue;
        }

        current->state = CC_CCM_FILE_VISITED;
        CCUINT32 index = frame.file;
        if (!ccm_dyn_push(
                &ctx->order,
                (CCUINT32)sizeof(CCUINT32),
                (CCINT32)sizeof(CCUINT32),
                &index
            ))
        {
            color_print(
                CC_TEXT_COLOR(CC_MAGENTA, CC_DEFAULT)
                "配置文件定位失败：内存分配失败\n"
            );
            goto Finished;
        }
        ccm_stack_pop(ctx);
    }

    ok = ccm_verify_result(ctx);

Finished:
    ccm_release_vfs(ctx);
    return ok;
}

void ccm_print_locations(CCMakeCCMContext *ctx)
{
    if (!ctx)
        return;

    CCUINT32 count = ctx->order.size / (CCUINT32)sizeof(CCUINT32);
    color_print(
        CC_TEXT_COLOR(CC_BCYAN, CC_DEFAULT)
        "配置文件定位完成：%u 个（解析顺序）\n",
        count
    );
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCUINT32 index = 0;
        if (cc_dyn_core_seek(
                &ctx->order,
                (CCUINT32)sizeof(CCUINT32),
                (CCINT32)sizeof(CCUINT32),
                &index, i
            ) != 0
        )
            continue;
        const CCMakeCCMFile *f = &((const CCMakeCCMFile *)ctx->files.array)[index];
        color_print("  %u. %s\n", i + 1, f->real_path);
    }
}
