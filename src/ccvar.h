/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 全局变量树（路径 API，内部模块使用）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCVAR_H_
#define _INCLUDE_CCVAR_H_

#include <crystal_cluster.h>

/* 变量树实例；节点结构不对外暴露 */
typedef struct cc_var_tree CCVarTree;

typedef enum {
    CC_VAR_NONE = 0,
    CC_VAR_OBJECT,
    CC_VAR_ARRAY,
    CC_VAR_STRING,
    CC_VAR_INT,
    CC_VAR_FLOAT,
    CC_VAR_BOOL
} CCVarType;

typedef enum {
    CC_VAR_OK         = 0,
    CC_VAR_ERR_PARAM  = -1,
    CC_VAR_ERR_NOENT  = -2,
    CC_VAR_ERR_TYPE   = -3,
    CC_VAR_ERR_NOSPACE = -4
} CCVarResult;

typedef enum {
    CC_VAR_WRITE_REPLACE = 0,  /* 清空旧值 / 旧子树后写入 */
    CC_VAR_WRITE_MERGE         /* 目标为对象时保留未提及的成员 */
} CCVarWriteMode;

/* 查询结果：字符串指向树内存储，立即使用或自行拷贝 */
typedef struct cc_var_value {
    CCVarType type;
    union {
        CCBOOL   boolean;
        CCINT64  integer;
        double   floating;
        struct {
            const char *data;
            CCUINT32    len;
        } string;
    } as;
} CCVarValue;

/* 子节点 / 数组元素遍历回调；返回非 0 中断遍历并透传 */
typedef CCINT32 (*CCVarChildVisit)(
    const char *key,
    CCUINT32 key_len,
    const CCVarValue *value,
    void *user
);

typedef CCINT32 (*CCVarElementVisit)(
    CCUINT32 index,
    const CCVarValue *value,
    void *user
);

/* 键名规则：[A-Za-z_][A-Za-z0-9_]*；大小写敏感 */
CCBOOL ccvar_key_valid(const char *key, CCUINT32 len);

/* 生命周期 */
CCVarTree *ccvar_create(void);
void ccvar_destroy(CCVarTree *tree);
void ccvar_clear(CCVarTree *tree);

/*
 * 写入一个值。
 * type / data 对应关系：
 *   CC_VAR_STRING: data 指向字节序列，len 为字节数；
 *   CC_VAR_INT:    data 指向 CCINT64；
 *   CC_VAR_FLOAT:  data 指向 double；
 *   CC_VAR_BOOL:   data 指向 CCBOOL；
 *   CC_VAR_OBJECT: data 为 NULL，mode 选择合并或替换；
 *   CC_VAR_ARRAY:  data 为 NULL，mode 忽略。
 * 对象合并、旧值清理、路径中间对象创建由树内部完成。
 * data 只读；字符串按 len 复制，其他类型按值复制，树不接管调用方指针。
 */
CCVarResult ccvar_set(
    CCVarTree *tree, const char *path,
    CCVarType type, const void *data,
    CCUINT32 len, CCVarWriteMode mode
);

/* 向已有数组追加一个值；type / data 约定同 ccvar_set */
CCVarResult ccvar_array_push(
    CCVarTree *tree, const char *array_path,
    CCVarType type, const void *data,
    CCUINT32 len, CCUINT32 *out_index
);

/* 查询 */
CCVarResult ccvar_get(const CCVarTree *tree, const char *path, CCVarValue *out);
CCVarType ccvar_type(const CCVarTree *tree, const char *path);
CCUINT32 ccvar_child_count(const CCVarTree *tree, const char *object_path);
CCINT32 ccvar_foreach_child(
    const CCVarTree *tree, const char *object_path,
    CCVarChildVisit cbk, void *user
);
CCUINT32 ccvar_array_size(const CCVarTree *tree, const char *array_path);
CCINT32 ccvar_foreach_element(
    const CCVarTree *tree, const char *array_path,
    CCVarElementVisit cbk, void *user
);

/* 删除整棵子树 */
CCVarResult ccvar_remove(CCVarTree *tree, const char *path);

/* 全局单例 */
/* 全局单例：由 main 在同一生命周期内成对调用 init / deinit */
CCBOOL ccvar_global_init(void);
CCVarTree *ccvar_global(void);
void ccvar_global_deinit(void);

#endif
