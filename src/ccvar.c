/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 全局变量树：路径写入、对象红黑树、数组顺序表、物理删除
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccvar_internal.h"

#include <stdio.h>
#include <string.h>

#define CC_VAR_TAG CC_DBG_REL_TAG("全局变量树", NULL)

/* ============================================================================
 * 基础访问
 * ========================================================================== */

static CCINT32 var_dyn_push(
    CCDyn *dyn, CCUINT32 align, CCINT32 size, const void *data
)
{
    if (!dyn || !data || size <= 0 || align == 0)
        return -1;
    if ((CCUINT32)size > (CCUINT32)-1 - dyn->size)
        return -1;
    return cc_dyn_core_push(dyn, align, size, (void *)data);
}

static CCUINT32 var_node_count(const CCVarTree *tree)
{
    return tree->nodes.size / (CCUINT32)sizeof(CCVarNode);
}

static CCVarNode *var_node_at(CCVarTree *tree, CCUINT32 index)
{
    return &((CCVarNode *)tree->nodes.array)[index];
}

static const CCVarNode *var_node_at_const(const CCVarTree *tree, CCUINT32 index)
{
    return &((const CCVarNode *)tree->nodes.array)[index];
}

static CCUINT32 var_idx_of(CCVarTree *tree, CCVarNode *node)
{
    return (CCUINT32)(node - (CCVarNode *)tree->nodes.array);
}

/* ============================================================================
 * 红黑树操作集：下标节点适配指针语义
 * ========================================================================== */

static CCINT32 var_rbt_compare_key(void *user, void *node, CCUINTPTR key)
{
    CC_UNUSED(user);
    return strcmp(((CCVarNode *)node)->name, (const char *)key);
}

static CCINT32 var_rbt_compare_node(void *user, void *node1, void *node2)
{
    CC_UNUSED(user);
    return strcmp(((CCVarNode *)node1)->name, ((CCVarNode *)node2)->name);
}

/* 删除双子节点时内核把后继数据拷入目标槽位；所有权随之转移，源槽位置空 */
static CCINT32 var_rbt_copy_node(void *user, void *dst_node, void *src_node)
{
    CC_UNUSED(user);
    CCVarNode *dst = (CCVarNode *)dst_node;
    CCVarNode *src = (CCVarNode *)src_node;

    dst->name = src->name;
    dst->name_len = src->name_len;
    dst->type = src->type;
    dst->parent = src->parent;
    dst->children_root = src->children_root;
    dst->elements = src->elements;
    dst->value = src->value;

    src->name = NULL;
    src->name_len = 0;
    src->type = CC_VAR_NONE;
    src->parent = CC_VAR_NIL;
    src->children_root = CC_VAR_NIL;
    memset(&src->elements, 0, sizeof(src->elements));
    memset(&src->value, 0, sizeof(src->value));
    return 0;
}

static void *var_rbt_find_instead(void *user, void *node)
{
    CCVarTree *tree = (CCVarTree *)user;
    CCVarNode *n = (CCVarNode *)node;
    if (n->rbt_right == CC_VAR_NIL)
        return NULL;
    CCUINT32 cur = n->rbt_right;
    while (var_node_at(tree, cur)->rbt_left != CC_VAR_NIL)
        cur = var_node_at(tree, cur)->rbt_left;
    return var_node_at(tree, cur);
}

static void *var_rbt_get_node(void *user, void *node, CC_RbtRel relation)
{
    CCVarTree *tree = (CCVarTree *)user;
    CCVarNode *n = (CCVarNode *)node;
    CCUINT32 idx;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:   idx = n->rbt_left; break;
    case CC_RBT_REL_RIGHT:  idx = n->rbt_right; break;
    case CC_RBT_REL_PARENT: idx = n->rbt_parent; break;
    default: return NULL;
    }
    return (idx == CC_VAR_NIL) ? NULL : var_node_at(tree, idx);
}

static CCUINT32 var_rbt_set_node(void *user, void *node1, void *node2, CC_RbtRel relation)
{
    CCVarTree *tree = (CCVarTree *)user;
    CCVarNode *n = (CCVarNode *)node1;
    CCUINT32 idx = node2 ? var_idx_of(tree, (CCVarNode *)node2) : CC_VAR_NIL;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:   n->rbt_left = idx; return 0;
    case CC_RBT_REL_RIGHT:  n->rbt_right = idx; return 0;
    case CC_RBT_REL_PARENT: n->rbt_parent = idx; return 0;
    default: return 1;
    }
}

static CCBOOL var_rbt_get_color(void *user, void *node)
{
    CC_UNUSED(user);
    return ((CCVarNode *)node)->is_red;
}

static CCINT32 var_rbt_set_color(void *user, void *node, CCBOOL is_red)
{
    CC_UNUSED(user);
    ((CCVarNode *)node)->is_red = is_red;
    return 0;
}

static CCINT32 var_rbt_left_rotate(void *user, void *node)
{
    CCVarTree *tree = (CCVarTree *)user;
    CCVarNode *x = (CCVarNode *)node;
    if (x->rbt_right == CC_VAR_NIL)
        return 1;
    CCUINT32 xi = var_idx_of(tree, x);
    CCUINT32 yi = x->rbt_right;
    CCVarNode *y = var_node_at(tree, yi);

    x->rbt_right = y->rbt_left;
    if (y->rbt_left != CC_VAR_NIL)
        var_node_at(tree, y->rbt_left)->rbt_parent = xi;

    y->rbt_parent = x->rbt_parent;
    if (x->rbt_parent != CC_VAR_NIL)
    {
        CCVarNode *pp = var_node_at(tree, x->rbt_parent);
        if (pp->rbt_left == xi)
            pp->rbt_left = yi;
        else
            pp->rbt_right = yi;
    }

    y->rbt_left = xi;
    x->rbt_parent = yi;
    return 0;
}

static CCINT32 var_rbt_right_rotate(void *user, void *node)
{
    CCVarTree *tree = (CCVarTree *)user;
    CCVarNode *x = (CCVarNode *)node;
    if (x->rbt_left == CC_VAR_NIL)
        return 1;
    CCUINT32 xi = var_idx_of(tree, x);
    CCUINT32 yi = x->rbt_left;
    CCVarNode *y = var_node_at(tree, yi);

    x->rbt_left = y->rbt_right;
    if (y->rbt_right != CC_VAR_NIL)
        var_node_at(tree, y->rbt_right)->rbt_parent = xi;

    y->rbt_parent = x->rbt_parent;
    if (x->rbt_parent != CC_VAR_NIL)
    {
        CCVarNode *pp = var_node_at(tree, x->rbt_parent);
        if (pp->rbt_left == xi)
            pp->rbt_left = yi;
        else
            pp->rbt_right = yi;
    }

    y->rbt_right = xi;
    x->rbt_parent = yi;
    return 0;
}

static void var_rbt_ch_root(void *user, void *new_root)
{
    CCVarTree *tree = (CCVarTree *)user;
    if (tree->rbt_owner == CC_VAR_NIL)
        return;
    CCVarNode *owner = var_node_at(tree, tree->rbt_owner);
    owner->children_root = new_root ? var_idx_of(tree, (CCVarNode *)new_root) : CC_VAR_NIL;
}

static CC_RbtOps var_rbt_ops = {
    .user = NULL,
    .compare_key = var_rbt_compare_key,
    .compare_node = var_rbt_compare_node,
    .copy_node = var_rbt_copy_node,
    .find_instead = var_rbt_find_instead,
    .get_node = var_rbt_get_node,
    .set_node = var_rbt_set_node,
    .get_color = var_rbt_get_color,
    .set_color = var_rbt_set_color,
    .left_rotate = var_rbt_left_rotate,
    .right_rotate = var_rbt_right_rotate,
    .ch_root = var_rbt_ch_root,
};

/* 每次红黑树调用前绑定上下文：节点运算需要 tree，ch_root 需要当前父对象 */
static void var_rbt_prepare(CCVarTree *tree, CCUINT32 owner)
{
    var_rbt_ops.user = tree;
    tree->rbt_owner = owner;
}

/* ============================================================================
 * 节点生命周期
 * ========================================================================== */

static CCVarResult var_node_push(
    CCVarTree *tree,
    CCVarType type,
    const char *name,
    CCUINT32 name_len,
    CCUINT32 parent,
    CCUINT32 *out
)
{
    CCVarNode node;
    memset(&node, 0, sizeof(node));
    node.type = type;
    node.parent = parent;
    node.children_root = CC_VAR_NIL;
    node.rbt_parent = CC_VAR_NIL;
    node.rbt_left = CC_VAR_NIL;
    node.rbt_right = CC_VAR_NIL;
    node.is_red = CCFALSE;

    if (name)
    {
        node.name = ccalloc(
            NULL, (CCUINT64)name_len + 1,
            CC_DBG_REL_TAG("变量键名", NULL)
        );
        if (!node.name)
            return CC_VAR_ERR_NOSPACE;
        memcpy(node.name, name, name_len);
        node.name[name_len] = '\0';
        node.name_len = name_len;
    }

    if (var_dyn_push(
            &tree->nodes,
            (CCUINT32)sizeof(CCVarNode),
            (CCINT32)sizeof(CCVarNode),
            &node
        ) != 0
    )
    {
        if (node.name)
            ccalloc(node.name, 0, NULL);
        return CC_VAR_ERR_NOSPACE;
    }

    *out = var_node_count(tree) - 1;
    return CC_VAR_OK;
}

static void var_node_free_payload(CCVarTree *tree, CCUINT32 index)
{
    CCVarNode *n = var_node_at(tree, index);
    if (n->name)
    {
        ccalloc(n->name, 0, NULL);
        n->name = NULL;
    }
    n->name_len = 0;
    if (n->type == CC_VAR_STRING && n->value.string.data)
    {
        ccalloc(n->value.string.data, 0, NULL);
        n->value.string.data = NULL;
        n->value.string.len = 0;
    }
    if (n->elements.array)
    {
        ccalloc(n->elements.array, 0, NULL);
        memset(&n->elements, 0, sizeof(n->elements));
    }
    n->type = CC_VAR_NONE;
    n->parent = CC_VAR_NIL;
    n->children_root = CC_VAR_NIL;
}

static CCVarResult var_rbt_insert(CCVarTree *tree, CCUINT32 owner, CCUINT32 child)
{
    CCVarNode *p = var_node_at(tree, owner);
    CCVarNode *root = (p->children_root == CC_VAR_NIL) ? NULL : var_node_at(tree, p->children_root);
    CCVarNode *node = var_node_at(tree, child);
    var_rbt_prepare(tree, owner);
    if (cc_rbt_core_insert(&var_rbt_ops, root, node) != 0)
        return CC_VAR_ERR_NOSPACE;
    return CC_VAR_OK;
}

static CCVarResult var_rbt_find(
    CCVarTree *tree,
    CCUINT32 owner,
    const char *key,
    CCUINT32 *out
)
{
    CCVarNode *p = var_node_at(tree, owner);
    if (p->children_root == CC_VAR_NIL)
        return CC_VAR_ERR_NOENT;

    var_rbt_prepare(tree, owner);
    CCVarNode *hit = (CCVarNode *)cc_rbt_core_get(
        &var_rbt_ops, var_node_at(tree, p->children_root), (CCUINTPTR)key
    );
    if (!hit)
        return CC_VAR_ERR_NOENT;
    *out = var_idx_of(tree, hit);
    return CC_VAR_OK;
}

/* ============================================================================
 * 键名与路径
 * ========================================================================== */

static CCBOOL var_key_start_char(char c)
{
    return (
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        c == '_'
    ) ? CCTRUE : CCFALSE;
}

static CCBOOL var_key_char(char c)
{
    return (var_key_start_char(c) || (c >= '0' && c <= '9')) ? CCTRUE : CCFALSE;
}

CCBOOL ccvar_key_valid(const char *key, CCUINT32 len)
{
    if (!key || len == 0 || !var_key_start_char(key[0]))
        return CCFALSE;
    for (CCUINT32 i = 1; i < len; i++)
    {
        if (!var_key_char(key[i]))
            return CCFALSE;
    }
    return CCTRUE;
}

typedef struct cc_var_path_seg {
    const char *key;   /* 指向 path 工作副本，已 NUL 结尾 */
    CCUINT32 index;
    CCBOOL is_index;
} CCVarPathSeg;

static void var_path_free(CCDyn *segs, char *work)
{
    if (segs && segs->array)
        ccalloc(segs->array, 0, NULL);
    if (work)
        ccalloc(work, 0, NULL);
}

static CCVarResult var_path_push_key(CCDyn *segs, const char *key)
{
    CCVarPathSeg seg;
    seg.key = key;
    seg.index = 0;
    seg.is_index = CCFALSE;
    return (var_dyn_push(
            segs, (CCUINT32)sizeof(CCVarPathSeg),
            (CCINT32)sizeof(CCVarPathSeg), &seg
        ) == 0
    ) ? CC_VAR_OK : CC_VAR_ERR_NOSPACE;
}

static CCVarResult var_path_push_index(CCDyn *segs, CCUINT32 index)
{
    CCVarPathSeg seg;
    seg.key = NULL;
    seg.index = index;
    seg.is_index = CCTRUE;
    return (var_dyn_push(
            segs, (CCUINT32)sizeof(CCVarPathSeg),
            (CCINT32)sizeof(CCVarPathSeg), &seg
        ) == 0
    ) ? CC_VAR_OK : CC_VAR_ERR_NOSPACE;
}

static CCVarResult var_path_parse(const char *path, CCDyn *segs, char **work_out)
{
    if (!path)
        return CC_VAR_ERR_PARAM;

    size_t len = strlen(path);
    char *work = ccalloc(NULL, (CCUINT64)len + 1, CC_DBG_REL_TAG("变量路径", NULL));
    if (!work)
        return CC_VAR_ERR_NOSPACE;
    memcpy(work, path, len + 1);
    memset(segs, 0, sizeof(*segs));

    if (len == 0)
    {
        *work_out = work;
        return CC_VAR_OK;
    }

    size_t pos = 0;
    CCBOOL expect_key = CCTRUE;
    while (pos < len)
    {
        if (expect_key)
        {
            if (!var_key_start_char(work[pos]))
                goto FailedtoParse;

            size_t key_begin = pos;
            pos++;
            while (pos < len && var_key_char(work[pos]))
                pos++;
            if (pos < len && work[pos] != '.' && work[pos] != '[')
                goto FailedtoParse;

            char delim = (pos < len) ? work[pos] : '\0';
            if (pos < len)
                work[pos] = '\0';
            if (var_path_push_key(segs, work + key_begin) != CC_VAR_OK)
                goto FailedtoParse;

            if (pos >= len)
                break;
            if (delim == '.')
            {
                pos++;
                if (pos >= len)
                    goto FailedtoParse;
                expect_key = CCTRUE;
            }
            else /* '[' */
            {
                pos++;
                expect_key = CCFALSE;
            }
            continue;
        }

        /* 数组下标 */
        size_t idx_begin = pos;
        CCUINT32 index = 0;
        while (pos < len && work[pos] >= '0' && work[pos] <= '9')
        {
            CCUINT32 digit = (CCUINT32)(work[pos] - '0');
            if (index > (0xFFFFFFFFu - digit) / 10)
                goto FailedtoParse;
            index = index * 10 + digit;
            pos++;
        }
        if (pos == idx_begin || pos >= len || work[pos] != ']')
            goto FailedtoParse;
        work[pos] = '\0';
        if (var_path_push_index(segs, index) != CC_VAR_OK)
            goto FailedtoParse;

        pos++;
        if (pos >= len)
            break;
        if (work[pos] == '.')
        {
            pos++;
            if (pos >= len)
                goto FailedtoParse;
            expect_key = CCTRUE;
        }
        else if (work[pos] == '[')
        {
            pos++;
            expect_key = CCFALSE;
        }
        else
        {
            goto FailedtoParse;
        }
    }

    *work_out = work;
    return CC_VAR_OK;

FailedtoParse:
    var_path_free(segs, work);
    return CC_VAR_ERR_PARAM;
}

static CCVarResult var_array_ref_at(
    CCVarTree *tree,
    CCUINT32 array,
    CCUINT32 index,
    CCUINT32 *out
)
{
    CCVarNode *n = var_node_at(tree, array);
    CCUINT32 count = n->elements.size / (CCUINT32)sizeof(CCUINT32);
    if (index >= count)
        return CC_VAR_ERR_NOENT;
    return (cc_dyn_core_seek(
            &n->elements,
            (CCUINT32)sizeof(CCUINT32),
            (CCINT32)sizeof(CCUINT32),
            out, index
        ) == 0
    ) ? CC_VAR_OK : CC_VAR_ERR_NOENT;
}

static CCVarResult var_object_child_create(
    CCVarTree *tree,
    CCUINT32 parent,
    const char *key,
    CCUINT32 key_len,
    CCVarType type,
    CCUINT32 *out
)
{
    CCUINT32 idx = CC_VAR_NIL;
    CCVarResult r = var_node_push(tree, type, key, key_len, parent, &idx);
    if (r != CC_VAR_OK)
        return r;

    r = var_rbt_insert(tree, parent, idx);
    if (r != CC_VAR_OK)
    {
        ccalloc(var_node_at(tree, idx)->name, 0, NULL);
        tree->nodes.size -= (CCUINT32)sizeof(CCVarNode);
        return r;
    }
    *out = idx;
    return CC_VAR_OK;
}

static CCVarResult var_array_element_create(
    CCVarTree *tree,
    CCUINT32 array,
    CCVarType type,
    CCUINT32 at,
    CCUINT32 *out
)
{
    CCUINT32 idx = CC_VAR_NIL;
    CCVarResult r = var_node_push(tree, type, NULL, 0, array, &idx);
    if (r != CC_VAR_OK)
        return r;

    CCVarNode *arr = var_node_at(tree, array);
    CCUINT32 count = arr->elements.size / (CCUINT32)sizeof(CCUINT32);
    if (at > count)
    {
        ccalloc(var_node_at(tree, idx)->name, 0, NULL);
        tree->nodes.size -= (CCUINT32)sizeof(CCVarNode);
        return CC_VAR_ERR_PARAM;
    }

    if (var_dyn_push(
            &arr->elements, (CCUINT32)sizeof(CCUINT32),
            (CCINT32)sizeof(CCUINT32), &idx
        ) != 0
    )
    {
        ccalloc(var_node_at(tree, idx)->name, 0, NULL);
        tree->nodes.size -= (CCUINT32)sizeof(CCVarNode);
        return CC_VAR_ERR_NOSPACE;
    }

    arr = var_node_at(tree, array);
    CCUINT32 *slots = (CCUINT32 *)arr->elements.array;
    for (CCUINT32 i = count; i > at; i--)
        slots[i] = slots[i - 1];
    slots[at] = idx;

    *out = idx;
    return CC_VAR_OK;
}

/* 遍历路径；create 为真时自动创建缺失的中间对象 */
static CCVarResult var_walk(
    CCVarTree *tree,
    const CCVarPathSeg *segs,
    CCUINT32 count,
    CCBOOL create,
    CCUINT32 *out
)
{
    CCUINT32 current = tree->root;
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCVarNode *n = var_node_at(tree, current);
        if (!segs[i].is_index)
        {
            if (n->type != CC_VAR_OBJECT)
                return CC_VAR_ERR_TYPE;
            CCUINT32 child = CC_VAR_NIL;
            CCVarResult r = var_rbt_find(tree, current, segs[i].key, &child);
            if (r == CC_VAR_ERR_NOENT && create)
            {
                r = var_object_child_create(
                    tree, current, segs[i].key,
                    (CCUINT32)strlen(segs[i].key),
                    CC_VAR_OBJECT, &child
                );
            }
            if (r != CC_VAR_OK)
                return r;
            current = child;
        }
        else
        {
            if (n->type != CC_VAR_ARRAY)
                return CC_VAR_ERR_TYPE;
            CCUINT32 child = CC_VAR_NIL;
            CCVarResult r = var_array_ref_at(tree, current, segs[i].index, &child);
            if (r != CC_VAR_OK)
                return r;
            current = child;
        }
    }
    *out = current;
    return CC_VAR_OK;
}

/* ============================================================================
 * 值填充
 * ========================================================================== */

static void var_fill_value(const CCVarNode *n, CCVarValue *out)
{
    memset(out, 0, sizeof(*out));
    out->type = n->type;
    switch (n->type)
    {
    case CC_VAR_BOOL:
        out->as.boolean = n->value.boolean;
        break;
    case CC_VAR_INT:
        out->as.integer = n->value.integer;
        break;
    case CC_VAR_FLOAT:
        out->as.floating = n->value.floating;
        break;
    case CC_VAR_STRING:
        out->as.string.data = n->value.string.data;
        out->as.string.len = n->value.string.len;
        break;
    default:
        break;
    }
}

/* ============================================================================
 * 删除：路径收集、叶子摘除、swap-remove
 * ========================================================================== */

static void var_reparent_children(CCVarTree *tree, CCUINT32 node, CCUINT32 new_parent)
{
    CCVarNode *n = var_node_at(tree, node);
    if (n->type == CC_VAR_OBJECT)
    {
        if (n->children_root == CC_VAR_NIL)
            return;
        CCDyn stack;
        memset(&stack, 0, sizeof(stack));
        CCUINT32 root = n->children_root;
        if (var_dyn_push(
                &stack, (CCUINT32)sizeof(CCUINT32),
                (CCINT32)sizeof(CCUINT32), &root
            ) != 0
        )
            return;
        while (stack.size >= (CCUINT32)sizeof(CCUINT32))
        {
            CCUINT32 cur = CC_VAR_NIL;
            if (cc_dyn_core_pop(
                    &stack, (CCUINT32)sizeof(CCUINT32),
                    (CCINT32)sizeof(CCUINT32), &cur
                ) != 0
            )
                break;
            CCVarNode *c = var_node_at(tree, cur);
            c->parent = new_parent;
            if (c->rbt_left != CC_VAR_NIL)
                var_dyn_push(
                    &stack, (CCUINT32)sizeof(CCUINT32),
                    (CCINT32)sizeof(CCUINT32), &c->rbt_left
                );
            if (c->rbt_right != CC_VAR_NIL)
                var_dyn_push(
                    &stack, (CCUINT32)sizeof(CCUINT32),
                    (CCINT32)sizeof(CCUINT32), &c->rbt_right
                );
        }
        if (stack.array)
            ccalloc(stack.array, 0, NULL);
    }
    else if (n->type == CC_VAR_ARRAY)
    {
        CCUINT32 count = n->elements.size / (CCUINT32)sizeof(CCUINT32);
        CCUINT32 *slots = (CCUINT32 *)n->elements.array;
        for (CCUINT32 i = 0; i < count; i++)
            var_node_at(tree, slots[i])->parent = new_parent;
    }
}

/* swap-remove 物理移除槽位 y；末尾节点搬到 y 并修正所有内部引用 */
static CCVarResult var_remove_slot(CCVarTree *tree, CCUINT32 y)
{
    CCUINT32 count = var_node_count(tree);
    if (y >= count)
        return CC_VAR_ERR_PARAM;
    CCUINT32 last = count - 1;
    if (y != last)
    {
        CCVarNode *lastn = var_node_at(tree, last);
        CCUINT32 parent = lastn->parent;
        if (parent == CC_VAR_NIL)
            return CC_VAR_ERR_PARAM;

        CCVarNode *pn = var_node_at(tree, parent);
        if (pn->type == CC_VAR_OBJECT)
        {
            if (lastn->rbt_parent == CC_VAR_NIL)
            {
                pn->children_root = y;
            }
            else
            {
                CCVarNode *rp = var_node_at(tree, lastn->rbt_parent);
                if (rp->rbt_left == last)
                    rp->rbt_left = y;
                else
                    rp->rbt_right = y;
            }
            if (lastn->rbt_left != CC_VAR_NIL)
                var_node_at(tree, lastn->rbt_left)->rbt_parent = y;
            if (lastn->rbt_right != CC_VAR_NIL)
                var_node_at(tree, lastn->rbt_right)->rbt_parent = y;
        }
        else if (pn->type == CC_VAR_ARRAY)
        {
            CCUINT32 ec = pn->elements.size / (CCUINT32)sizeof(CCUINT32);
            CCUINT32 *slots = (CCUINT32 *)pn->elements.array;
            CCBOOL fixed = CCFALSE;
            for (CCUINT32 i = 0; i < ec; i++)
            {
                if (slots[i] == last)
                {
                    slots[i] = y;
                    fixed = CCTRUE;
                    break;
                }
            }
            if (!fixed)
                return CC_VAR_ERR_PARAM;
        }
        else
        {
            return CC_VAR_ERR_PARAM;
        }

        var_reparent_children(tree, last, y);
        *var_node_at(tree, y) = *lastn;
    }
    tree->nodes.size -= (CCUINT32)sizeof(CCVarNode);
    return CC_VAR_OK;
}

/* 从父对象红黑树中删除一个叶子节点，释放原目标载荷，再做 swap-remove */
static CCVarResult var_remove_object_leaf(
    CCVarTree *tree,
    CCUINT32 parent,
    const char *key,
    CCUINT32 child
)
{
    CCVarNode *z = var_node_at(tree, child);
    if (z->children_root != CC_VAR_NIL || z->elements.size != 0)
        return CC_VAR_ERR_TYPE;

    char *z_name = z->name;
    CCUINT32 z_name_len = z->name_len;
    CCVarType z_type = z->type;
    char *z_string = (z_type == CC_VAR_STRING) ? z->value.string.data : NULL;
    CCDyn z_elements = z->elements;

    CCVarNode *p = var_node_at(tree, parent);
    CCVarNode *root = var_node_at(tree, p->children_root);
    var_rbt_prepare(tree, parent);
    CCVarNode *y = (CCVarNode *)cc_rbt_core_remove(
        &var_rbt_ops, root, (CCUINTPTR)key
    );
    if (!y)
        return CC_VAR_ERR_NOENT;
    CCUINT32 y_idx = var_idx_of(tree, y);

    if (y_idx != child)
        var_reparent_children(tree, child, child);

    if (z_name)
        ccalloc(z_name, 0, NULL);
    (void)z_name_len;
    if (z_string)
        ccalloc(z_string, 0, NULL);
    if (z_elements.array)
        ccalloc(z_elements.array, 0, NULL);

    return var_remove_slot(tree, y_idx);
}

/* 删除一个叶子节点；调用前其后代必须已删除 */
static CCVarResult var_remove_one(CCVarTree *tree, const char *path)
{
    CCDyn segs;
    memset(&segs, 0, sizeof(segs));
    char *work = NULL;
    CCVarResult r = var_path_parse(path, &segs, &work);
    if (r != CC_VAR_OK)
        return r;

    CCUINT32 count = segs.size / (CCUINT32)sizeof(CCVarPathSeg);
    if (count == 0)
    {
        var_path_free(&segs, work);
        return CC_VAR_ERR_PARAM;
    }

    CCVarPathSeg *sa = (CCVarPathSeg *)segs.array;
    CCUINT32 parent = tree->root;
    if (count > 1)
    {
        CCVarResult wr = var_walk(tree, sa, count - 1, CCFALSE, &parent);
        if (wr != CC_VAR_OK)
        {
            var_path_free(&segs, work);
            return wr;
        }
    }

    CCVarPathSeg *last = &sa[count - 1];
    CCVarResult wr = CC_VAR_OK;
    if (!last->is_index)
    {
        CCUINT32 child = CC_VAR_NIL;
        wr = var_rbt_find(tree, parent, last->key, &child);
        if (wr != CC_VAR_OK)
        {
            var_path_free(&segs, work);
            return wr;
        }
        r = var_remove_object_leaf(tree, parent, last->key, child);
    }
    else
    {
        CCUINT32 child = CC_VAR_NIL;
        wr = var_array_ref_at(tree, parent, last->index, &child);
        if (wr != CC_VAR_OK)
        {
            var_path_free(&segs, work);
            return wr;
        }
        CCVarNode *cn = var_node_at(tree, child);
        if (cn->children_root != CC_VAR_NIL || cn->elements.size != 0)
        {
            var_path_free(&segs, work);
            return CC_VAR_ERR_TYPE;
        }

        CCVarNode *arr = var_node_at(tree, parent);
        CCUINT32 removed = CC_VAR_NIL;
        if (cc_dyn_core_get(
                &arr->elements,
                (CCUINT32)sizeof(CCUINT32),
                (CCINT32)sizeof(CCUINT32),
                &removed, last->index
            ) != 0
        )
        {
            var_path_free(&segs, work);
            return CC_VAR_ERR_NOENT;
        }

        var_node_free_payload(tree, child);
        r = var_remove_slot(tree, child);
    }

    var_path_free(&segs, work);
    return r;
}

static char *var_path_key(const char *base, const char *key, CCUINT32 key_len)
{
    size_t base_len = base ? strlen(base) : 0;
    size_t need = base_len + (base_len ? 1 : 0) + key_len + 1;
    char *out = ccalloc(NULL, need, CC_DBG_REL_TAG("变量路径", NULL));
    if (!out)
        return NULL;
    size_t pos = 0;
    if (base_len)
    {
        memcpy(out, base, base_len);
        pos = base_len;
        out[pos++] = '.';
    }
    memcpy(out + pos, key, key_len);
    out[pos + key_len] = '\0';
    return out;
}

static char *var_path_index(const char *base, CCUINT32 index)
{
    char num[16];
    int n = snprintf(num, sizeof(num), "[%u]", index);
    if (n < 0)
        return NULL;
    size_t base_len = base ? strlen(base) : 0;
    char *out = ccalloc(
        NULL, (CCUINT64)base_len + (CCUINT64)n + 1,
        CC_DBG_REL_TAG("变量路径", NULL)
    );
    if (!out)
        return NULL;
    memcpy(out, base, base_len);
    memcpy(out + base_len, num, (size_t)n + 1);
    return out;
}

typedef struct cc_var_collect_item {
    CCUINT32 node;
    char *path;
} CCVarCollectItem;

typedef struct cc_var_kid_ctx {
    CCVarTree *tree;
    CCDyn *kids;
} CCVarKidCtx;

static CCINT32 var_collect_kid(void *data, void *user)
{
    CCVarKidCtx *ctx = (CCVarKidCtx *)user;
    CCVarNode *n = (CCVarNode *)data;
    CCUINT32 idx = var_idx_of(ctx->tree, n);
    return (var_dyn_push(ctx->kids, (CCUINT32)sizeof(CCUINT32),
                             (CCINT32)sizeof(CCUINT32), &idx) == 0)
               ? 0 : 1;
}

static CCVarResult var_collect_paths(
    CCVarTree *tree,
    CCUINT32 root,
    const char *root_path,
    CCDyn *paths
)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));

    CCVarCollectItem first;
    first.node = root;
    first.path = ccalloc(
        NULL, (CCUINT64)strlen(root_path) + 1,
        CC_DBG_REL_TAG("变量删除路径", NULL)
    );
    if (!first.path)
        return CC_VAR_ERR_NOSPACE;
    memcpy(first.path, root_path, strlen(root_path) + 1);

    if (var_dyn_push(
            &stack, (CCUINT32)sizeof(CCVarCollectItem),
            (CCINT32)sizeof(CCVarCollectItem), &first
        ) != 0
    )
    {
        ccalloc(first.path, 0, NULL);
        return CC_VAR_ERR_NOSPACE;
    }

    CCVarResult ret = CC_VAR_OK;
    while (stack.size >= (CCUINT32)sizeof(CCVarCollectItem))
    {
        CCVarCollectItem item;
        if (cc_dyn_core_pop(
                &stack, (CCUINT32)sizeof(CCVarCollectItem),
                (CCINT32)sizeof(CCVarCollectItem), &item
            ) != 0
        )
        {
            ret = CC_VAR_ERR_PARAM;
            break;
        }

        if (var_dyn_push(
                paths, (CCUINT32)sizeof(char *),
                (CCINT32)sizeof(char *), &item.path
            ) != 0
        )
        {
            ccalloc(item.path, 0, NULL);
            ret = CC_VAR_ERR_NOSPACE;
            break;
        }

        CCVarNode *n = var_node_at(tree, item.node);
        if (n->type == CC_VAR_OBJECT && n->children_root != CC_VAR_NIL)
        {
            /* 先序：父先入表，子节点随后入栈 */
            CCDyn kids;
            memset(&kids, 0, sizeof(kids));
            CCVarKidCtx kc;
            kc.tree = tree;
            kc.kids = &kids;
            var_rbt_prepare(tree, item.node);
            if (cc_rbt_core_inorder_tranverse(
                    &var_rbt_ops,
                    var_node_at(tree, n->children_root),
                    &kc, var_collect_kid
                ) != 0
            )
            {
                if (kids.array)
                    ccalloc(kids.array, 0, NULL);
                ret = CC_VAR_ERR_NOSPACE;
                break;
            }

            CCUINT32 kc_count = kids.size / (CCUINT32)sizeof(CCUINT32);
            CCUINT32 *ka = (CCUINT32 *)kids.array;
            /* 逆序压栈，弹出时恢复中序；对象顺序不影响删除 */
            for (CCUINT32 i = 0; i < kc_count; i++)
            {
                CCUINT32 child = ka[kc_count - 1 - i];
                CCVarNode *cn = var_node_at(tree, child);
                char *cp = var_path_key(item.path, cn->name, cn->name_len);
                if (!cp)
                {
                    ret = CC_VAR_ERR_NOSPACE;
                    break;
                }
                CCVarCollectItem ci;
                ci.node = child;
                ci.path = cp;
                if (var_dyn_push(
                        &stack, (CCUINT32)sizeof(CCVarCollectItem),
                        (CCINT32)sizeof(CCVarCollectItem), &ci
                    ) != 0
                )
                {
                    ccalloc(cp, 0, NULL);
                    ret = CC_VAR_ERR_NOSPACE;
                    break;
                }
            }
            if (kids.array)
                ccalloc(kids.array, 0, NULL);
            if (ret != CC_VAR_OK)
                break;
        }
        else if (n->type == CC_VAR_ARRAY)
        {
            CCUINT32 ec = n->elements.size / (CCUINT32)sizeof(CCUINT32);
            CCUINT32 *slots = (CCUINT32 *)n->elements.array;
            /* 逆序压栈，弹出时从 0 到 n-1 入表；反转删除即从 n-1 到 0 */
            for (CCUINT32 i = ec; i > 0; i--)
            {
                CCUINT32 child = slots[i - 1];
                char *cp = var_path_index(item.path, i - 1);
                if (!cp)
                {
                    ret = CC_VAR_ERR_NOSPACE;
                    break;
                }
                CCVarCollectItem ci;
                ci.node = child;
                ci.path = cp;
                if (var_dyn_push(
                        &stack, (CCUINT32)sizeof(CCVarCollectItem),
                        (CCINT32)sizeof(CCVarCollectItem), &ci
                    ) != 0
                )
                {
                    ccalloc(cp, 0, NULL);
                    ret = CC_VAR_ERR_NOSPACE;
                    break;
                }
            }
            if (ret != CC_VAR_OK)
                break;
        }
    }

    if (stack.size)
    {
        /* 失败时释放栈中剩余路径 */
        CCVarCollectItem *it = (CCVarCollectItem *)stack.array;
        CCUINT32 sc = stack.size / (CCUINT32)sizeof(CCVarCollectItem);
        for (CCUINT32 i = 0; i < sc; i++)
        {
            if (it[i].path)
                ccalloc(it[i].path, 0, NULL);
        }
    }
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return ret;
}

/* ============================================================================
 * 写入
 * ========================================================================== */

static CCVarResult var_attach(
    CCVarTree *tree,
    const CCVarPathSeg *segs,
    CCUINT32 count,
    CCVarType type,
    char *string_value,
    CCUINT32 string_len,
    CCINT64 integer,
    double floating,
    CCBOOL boolean
)
{
    CCUINT32 parent = tree->root;
    CCVarResult r = CC_VAR_OK;
    if (count > 1)
    {
        r = var_walk(tree, segs, count - 1, CCTRUE, &parent);
        if (r != CC_VAR_OK)
            return r;
    }

    const CCVarPathSeg *last = &segs[count - 1];
    CCVarNode *pn = var_node_at(tree, parent);
    CCUINT32 node = CC_VAR_NIL;

    if (!last->is_index)
    {
        if (pn->type != CC_VAR_OBJECT)
            return CC_VAR_ERR_TYPE;
        CCUINT32 exist = CC_VAR_NIL;
        if (var_rbt_find(tree, parent, last->key, &exist) == CC_VAR_OK)
            return CC_VAR_ERR_PARAM; /* 写入前应已删除旧值 */
        r = var_object_child_create(
            tree, parent, last->key,
            (CCUINT32)strlen(last->key), type, &node
        );
    }
    else
    {
        if (pn->type != CC_VAR_ARRAY)
            return CC_VAR_ERR_TYPE;
        r = var_array_element_create(tree, parent, type, last->index, &node);
    }
    if (r != CC_VAR_OK)
        return r;

    CCVarNode *n = var_node_at(tree, node);
    if (type == CC_VAR_STRING)
    {
        n->value.string.data = string_value;
        n->value.string.len = string_len;
    }
    else if (type == CC_VAR_INT)
        n->value.integer = integer;
    else if (type == CC_VAR_FLOAT)
        n->value.floating = floating;
    else if (type == CC_VAR_BOOL)
        n->value.boolean = boolean;
    return CC_VAR_OK;
}

static CCVarResult var_prepare(
    CCVarTree *tree,
    const char *path,
    CCVarType new_type,
    CCVarWriteMode mode,
    CCBOOL *skip
)
{
    *skip = CCFALSE;
    CCVarType cur = ccvar_type(tree, path);
    if (new_type == CC_VAR_OBJECT && mode == CC_VAR_WRITE_MERGE &&
        cur == CC_VAR_OBJECT)
    {
        *skip = CCTRUE;
        return CC_VAR_OK;
    }
    if (cur != CC_VAR_NONE)
    {
        CCVarResult r = ccvar_remove(tree, path);
        if (r != CC_VAR_OK)
            return r;
    }
    return CC_VAR_OK;
}

static CCVarResult var_set_parsed(
    CCVarTree *tree,
    const char *path,
    CCVarType type,
    const char *string_value,
    CCUINT32 string_len,
    CCINT64 integer,
    double floating,
    CCBOOL boolean
)
{
    CCDyn segs;
    memset(&segs, 0, sizeof(segs));
    char *work = NULL;
    CCVarResult r = var_path_parse(path, &segs, &work);
    if (r != CC_VAR_OK)
        return r;
    CCUINT32 count = segs.size / (CCUINT32)sizeof(CCVarPathSeg);
    if (count == 0)
    {
        var_path_free(&segs, work);
        return CC_VAR_ERR_PARAM;
    }
    char *copy = NULL;
    if (type == CC_VAR_STRING)
    {
        copy = ccalloc(
            NULL, (CCUINT64)string_len + 1,
            CC_DBG_REL_TAG("变量字符串", NULL)
        );
        if (!copy)
        {
            var_path_free(&segs, work);
            return CC_VAR_ERR_NOSPACE;
        }
        if (string_len)
            memcpy(copy, string_value, string_len);
        copy[string_len] = '\0';
    }

    r = var_attach(
        tree, (CCVarPathSeg *)segs.array, count, type,
        copy, string_len, integer, floating, boolean
    );
    if (r != CC_VAR_OK && copy)
        ccalloc(copy, 0, NULL);
    var_path_free(&segs, work);
    return r;
}

CCVarResult ccvar_set(CCVarTree *tree, const char *path,
                      CCVarType type, const void *data,
                      CCUINT32 len, CCVarWriteMode mode)
{
    if (!tree || !path)
        return CC_VAR_ERR_PARAM;

    const char *string_value = NULL;
    CCUINT32 string_len = 0;
    CCINT64 integer = 0;
    double floating = 0;
    CCBOOL boolean = CCFALSE;

    switch (type)
    {
    case CC_VAR_STRING:
        if (!data && len > 0)
            return CC_VAR_ERR_PARAM;
        string_value = data ? (const char *)data : "";
        string_len = len;
        break;
    case CC_VAR_INT:
        if (!data)
            return CC_VAR_ERR_PARAM;
        integer = *(const CCINT64 *)data;
        break;
    case CC_VAR_FLOAT:
        if (!data)
            return CC_VAR_ERR_PARAM;
        floating = *(const double *)data;
        break;
    case CC_VAR_BOOL:
        if (!data)
            return CC_VAR_ERR_PARAM;
        boolean = *(const CCBOOL *)data;
        break;
    case CC_VAR_OBJECT:
    case CC_VAR_ARRAY:
        break;
    default:
        return CC_VAR_ERR_PARAM;
    }

    CCVarWriteMode effective = (type == CC_VAR_OBJECT)
                                   ? mode : CC_VAR_WRITE_REPLACE;
    CCBOOL skip = CCFALSE;
    CCVarResult r = var_prepare(tree, path, type, effective, &skip);
    if (r != CC_VAR_OK || skip)
        return r;
    return var_set_parsed(
        tree, path, type, string_value, string_len,
        integer, floating, boolean
    );
}

static CCVarResult var_array_push_common(
    CCVarTree *tree,
    const char *array_path,
    CCVarType type,
    const char *string_value,
    CCUINT32 string_len,
    CCINT64 integer,
    double floating,
    CCBOOL boolean,
    CCUINT32 *out_index
)
{
    CCDyn segs;
    memset(&segs, 0, sizeof(segs));
    char *work = NULL;
    CCVarResult r = var_path_parse(array_path, &segs, &work);
    if (r != CC_VAR_OK)
        return r;

    CCUINT32 count = segs.size / (CCUINT32)sizeof(CCVarPathSeg);
    CCUINT32 array = tree->root;
    r = var_walk(tree, (CCVarPathSeg *)segs.array, count, CCFALSE, &array);
    if (r != CC_VAR_OK)
    {
        var_path_free(&segs, work);
        return r;
    }
    var_path_free(&segs, work);

    CCVarNode *an = var_node_at(tree, array);
    if (an->type != CC_VAR_ARRAY)
        return CC_VAR_ERR_TYPE;

    CCUINT32 count_elems = an->elements.size / (CCUINT32)sizeof(CCUINT32);
    char *copy = NULL;
    if (type == CC_VAR_STRING)
    {
        copy = ccalloc(
            NULL, (CCUINT64)string_len + 1,
            CC_DBG_REL_TAG("变量字符串", NULL)
        );
        if (!copy)
            return CC_VAR_ERR_NOSPACE;
        if (string_len)
            memcpy(copy, string_value, string_len);
        copy[string_len] = '\0';
    }

    CCUINT32 node = CC_VAR_NIL;
    r = var_array_element_create(tree, array, type, count_elems, &node);
    if (r != CC_VAR_OK)
    {
        if (copy)
            ccalloc(copy, 0, NULL);
        return r;
    }

    CCVarNode *n = var_node_at(tree, node);
    if (type == CC_VAR_STRING)
    {
        n->value.string.data = copy;
        n->value.string.len = string_len;
    }
    else if (type == CC_VAR_INT)
        n->value.integer = integer;
    else if (type == CC_VAR_FLOAT)
        n->value.floating = floating;
    else if (type == CC_VAR_BOOL)
        n->value.boolean = boolean;

    if (out_index)
        *out_index = count_elems;
    return CC_VAR_OK;
}

CCVarResult ccvar_array_push(CCVarTree *tree, const char *array_path,
                             CCVarType type, const void *data,
                             CCUINT32 len, CCUINT32 *out_index)
{
    if (!tree || !array_path)
        return CC_VAR_ERR_PARAM;

    const char *string_value = NULL;
    CCUINT32 string_len = 0;
    CCINT64 integer = 0;
    double floating = 0;
    CCBOOL boolean = CCFALSE;

    switch (type)
    {
    case CC_VAR_STRING:
        if (!data && len > 0)
            return CC_VAR_ERR_PARAM;
        string_value = data ? (const char *)data : "";
        string_len = len;
        break;
    case CC_VAR_INT:
        if (!data)
            return CC_VAR_ERR_PARAM;
        integer = *(const CCINT64 *)data;
        break;
    case CC_VAR_FLOAT:
        if (!data)
            return CC_VAR_ERR_PARAM;
        floating = *(const double *)data;
        break;
    case CC_VAR_BOOL:
        if (!data)
            return CC_VAR_ERR_PARAM;
        boolean = *(const CCBOOL *)data;
        break;
    case CC_VAR_OBJECT:
    case CC_VAR_ARRAY:
        break;
    default:
        return CC_VAR_ERR_PARAM;
    }

    return var_array_push_common(
        tree, array_path, type,
        string_value, string_len,
        integer, floating, boolean, out_index
    );
}

/* ============================================================================
 * 查询
 * ========================================================================== */

static CCVarResult var_resolve(
    const CCVarTree *tree,
    const char *path,
    CCUINT32 *out
)
{
    CCDyn segs;
    memset(&segs, 0, sizeof(segs));
    char *work = NULL;
    CCVarResult r = var_path_parse(path, &segs, &work);
    if (r != CC_VAR_OK)
        return r;
    CCUINT32 count = segs.size / (CCUINT32)sizeof(CCVarPathSeg);
    r = var_walk((CCVarTree *)tree, (CCVarPathSeg *)segs.array, count,
                 CCFALSE, out);
    var_path_free(&segs, work);
    return r;
}

CCVarResult ccvar_get(const CCVarTree *tree, const char *path, CCVarValue *out)
{
    if (!tree || !path || !out)
        return CC_VAR_ERR_PARAM;
    CCUINT32 idx = CC_VAR_NIL;
    CCVarResult r = var_resolve(tree, path, &idx);
    if (r != CC_VAR_OK)
        return r;
    var_fill_value(var_node_at_const(tree, idx), out);
    return CC_VAR_OK;
}

CCVarType ccvar_type(const CCVarTree *tree, const char *path)
{
    CCVarValue value;
    if (ccvar_get(tree, path, &value) != CC_VAR_OK)
        return CC_VAR_NONE;
    return value.type;
}

typedef struct cc_var_child_ctx {
    CCVarChildVisit cbk;
    void *user;
} CCVarChildCtx;

typedef struct cc_var_count_ctx {
    CCUINT32 count;
} CCVarCountCtx;

static CCINT32 var_child_visit(void *data, void *user)
{
    CCVarChildCtx *ctx = (CCVarChildCtx *)user;
    const CCVarNode *n = (const CCVarNode *)data;
    CCVarValue value;
    var_fill_value(n, &value);
    return ctx->cbk(n->name, n->name_len, &value, ctx->user);
}

static CCINT32 var_child_count_visit(void *data, void *user)
{
    CC_UNUSED(data);
    CCVarCountCtx *ctx = (CCVarCountCtx *)user;
    ctx->count++;
    return 0;
}

CCUINT32 ccvar_child_count(const CCVarTree *tree, const char *object_path)
{
    if (!tree || !object_path)
        return 0;
    CCUINT32 idx = CC_VAR_NIL;
    if (var_resolve(tree, object_path, &idx) != CC_VAR_OK)
        return 0;
    const CCVarNode *n = var_node_at_const(tree, idx);
    if (n->type != CC_VAR_OBJECT || n->children_root == CC_VAR_NIL)
        return 0;

    CCVarTree *mutable_tree = (CCVarTree *)tree;
    var_rbt_prepare(mutable_tree, idx);
    CCVarCountCtx ctx;
    ctx.count = 0;
    (void)cc_rbt_core_inorder_tranverse(
        &var_rbt_ops,
        var_node_at(mutable_tree, n->children_root),
        &ctx, var_child_count_visit
    );
    return ctx.count;
}

CCINT32 ccvar_foreach_child(const CCVarTree *tree, const char *object_path,
                            CCVarChildVisit cbk, void *user)
{
    if (!tree || !object_path || !cbk)
        return CC_VAR_ERR_PARAM;
    CCUINT32 idx = CC_VAR_NIL;
    CCVarResult r = var_resolve(tree, object_path, &idx);
    if (r != CC_VAR_OK)
        return r;
    const CCVarNode *n = var_node_at_const(tree, idx);
    if (n->type != CC_VAR_OBJECT)
        return CC_VAR_ERR_TYPE;
    if (n->children_root == CC_VAR_NIL)
        return 0;

    CCVarTree *mutable_tree = (CCVarTree *)tree;
    var_rbt_prepare(mutable_tree, idx);
    CCVarChildCtx ctx;
    ctx.cbk = cbk;
    ctx.user = user;
    return cc_rbt_core_inorder_tranverse(
        &var_rbt_ops,
        var_node_at(mutable_tree, n->children_root),
        &ctx, var_child_visit
    );
}

CCUINT32 ccvar_array_size(const CCVarTree *tree, const char *array_path)
{
    if (!tree || !array_path)
        return 0;
    CCUINT32 idx = CC_VAR_NIL;
    if (var_resolve(tree, array_path, &idx) != CC_VAR_OK)
        return 0;
    const CCVarNode *n = var_node_at_const(tree, idx);
    if (n->type != CC_VAR_ARRAY)
        return 0;
    return n->elements.size / (CCUINT32)sizeof(CCUINT32);
}

CCINT32 ccvar_foreach_element(const CCVarTree *tree, const char *array_path,
                              CCVarElementVisit cbk, void *user)
{
    if (!tree || !array_path || !cbk)
        return CC_VAR_ERR_PARAM;
    CCUINT32 idx = CC_VAR_NIL;
    CCVarResult r = var_resolve(tree, array_path, &idx);
    if (r != CC_VAR_OK)
        return r;
    const CCVarNode *n = var_node_at_const(tree, idx);
    if (n->type != CC_VAR_ARRAY)
        return CC_VAR_ERR_TYPE;

    CCUINT32 count = n->elements.size / (CCUINT32)sizeof(CCUINT32);
    CCUINT32 *slots = (CCUINT32 *)n->elements.array;
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCVarValue value;
        var_fill_value(var_node_at_const(tree, slots[i]), &value);
        CCINT32 rc = cbk(i, &value, user);
        if (rc != 0)
            return rc;
    }
    return 0;
}

/* ============================================================================
 * 删除入口
 * ========================================================================== */

CCVarResult ccvar_remove(CCVarTree *tree, const char *path)
{
    if (!tree || !path)
        return CC_VAR_ERR_PARAM;

    CCUINT32 target = CC_VAR_NIL;
    CCVarResult r = var_resolve(tree, path, &target);
    if (r != CC_VAR_OK)
        return r;
    if (target == tree->root)
        return CC_VAR_ERR_PARAM;

    CCDyn paths;
    memset(&paths, 0, sizeof(paths));
    r = var_collect_paths(tree, target, path, &paths);
    if (r != CC_VAR_OK)
    {
        if (paths.array)
        {
            char **ps = (char **)paths.array;
            CCUINT32 pc = paths.size / (CCUINT32)sizeof(char *);
            for (CCUINT32 i = 0; i < pc; i++)
            {
                if (ps[i])
                    ccalloc(ps[i], 0, NULL);
            }
            ccalloc(paths.array, 0, NULL);
        }
        return r;
    }

    r = CC_VAR_OK;
    CCUINT32 pc = paths.size / (CCUINT32)sizeof(char *);
    for (CCUINT32 i = pc; i > 0; i--)
    {
        char *p = ((char **)paths.array)[i - 1];
        CCVarResult dr = var_remove_one(tree, p);
        if (dr != CC_VAR_OK)
            r = dr;
    }

    char **ps = (char **)paths.array;
    for (CCUINT32 i = 0; i < pc; i++)
    {
        if (ps[i])
            ccalloc(ps[i], 0, NULL);
    }
    ccalloc(paths.array, 0, NULL);
    return r;
}

/* ============================================================================
 * 生命周期
 * ========================================================================== */

static CCVarResult var_init_root(CCVarTree *tree)
{
    CCUINT32 root = CC_VAR_NIL;
    CCVarResult r = var_node_push(
        tree, CC_VAR_OBJECT, NULL, 0,
        CC_VAR_NIL, &root
    );
    if (r != CC_VAR_OK)
        return r;
    tree->root = root;
    return CC_VAR_OK;
}

CCVarTree *ccvar_create(void)
{
    CCVarTree *tree = ccalloc(NULL, sizeof(CCVarTree), CC_VAR_TAG);
    if (!tree)
        return NULL;
    memset(tree, 0, sizeof(*tree));
    tree->root = CC_VAR_NIL;
    tree->rbt_owner = CC_VAR_NIL;
    if (var_init_root(tree) != CC_VAR_OK)
    {
        if (tree->nodes.array)
            ccalloc(tree->nodes.array, 0, NULL);
        ccalloc(tree, 0, NULL);
        return NULL;
    }
    return tree;
}

static void var_free_all(CCVarTree *tree)
{
    CCUINT32 count = var_node_count(tree);
    /* 节点按创建顺序存放，释放走逆序 */
    for (CCUINT32 i = count; i > 0; i--)
        var_node_free_payload(tree, i - 1);
    if (tree->nodes.array)
        ccalloc(tree->nodes.array, 0, NULL);
    memset(&tree->nodes, 0, sizeof(tree->nodes));
    tree->root = CC_VAR_NIL;
    tree->rbt_owner = CC_VAR_NIL;
}

CCVarResult ccvar_clear(CCVarTree *tree)
{
    if (!tree)
        return CC_VAR_ERR_PARAM;
    var_free_all(tree);
    return var_init_root(tree);
}

void ccvar_destroy(CCVarTree *tree)
{
    if (!tree)
        return;
    var_free_all(tree);
    ccalloc(tree, 0, NULL);
}

/* ============================================================================
 * 全局单例
 * ========================================================================== */

static CCVarTree *g_var_tree = NULL;

CCBOOL ccvar_global_init(void)
{
    if (g_var_tree)
        return CCTRUE;
    g_var_tree = ccvar_create();
    return g_var_tree ? CCTRUE : CCFALSE;
}

CCVarTree *ccvar_global(void)
{
    return g_var_tree;
}

void ccvar_global_deinit(void)
{
    if (g_var_tree)
    {
        ccvar_destroy(g_var_tree);
        g_var_tree = NULL;
    }
}
