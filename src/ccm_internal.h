/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .ccm 文件解析的内部数据结构与接口（不对外公开）
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCM_INTERNAL_H_
#define _INCLUDE_CCM_INTERNAL_H_

#include "ccmake_header.h"
#include "ccvar.h"

/* 文件 / include 边下标哨兵 */
#define CC_CCM_NIL 0xFFFFFFFFu

/* 文件在 include 闭包遍历中的状态 */
typedef enum {
    CC_CCM_FILE_UNSEEN = 0,  /* 已登记，尚未装载 */
    CC_CCM_FILE_VISITING,    /* 正在处理其 include（位于遍历栈上） */
    CC_CCM_FILE_VISITED      /* 已完成，已进入解析顺序 */
} CC_CCM_FILE_STATE;

/* 一条 @include 指令：literal 来自文件内容，resolved/child 在定位时填充 */
typedef struct ccmake_ccm_include_struct {
    char     *literal;    /* 原始路径字符串（ccalloc 分配） */
    CCUINT32  line;       /* @include 所在行（1 基） */
    CCUINT32  column;     /* @include 所在字节列（1 基） */
    CCUINT32  child;      /* 目标文件在 files 中的下标；CC_CCM_NIL 表示未解析 */
} CCMakeCCMInclude;

/* 一个被定位的 .ccm 文件；content 是阶段一交给阶段二的内容快照 */
typedef struct ccmake_ccm_file_struct {
    char     *real_path;     /* 规范化绝对宿主路径，'/' 分隔，保留大小写 */
    char     *v_path;        /* VFS 合成挂载点，如 "/__ccm:0" */
    char     *content;       /* 文件字节 + 结尾 0；已去 UTF-8 BOM */
    CCUINT64  size;          /* content 有效字节数 */
    CCUINT32  include_first; /* 本文件 include 边在 includes 中的起始下标 */
    CCUINT32  include_count; /* 本文件 include 边数量 */
    CCUINT32  parent;        /* 首次包含它的文件下标；根为 CC_CCM_NIL */
    CCUINT32  via_line;      /* 在 parent 中的 @include 行号；根为 0 */
    CCUINT32  depth;         /* include 深度，根为 0 */
    CC_CCM_FILE_STATE state;
    CCBOOL    mounted;       /* 是否已在 VFS 中建立合成挂载点 */
} CCMakeCCMFile;

/* 配置解析资源预算；所有上限在分配前检查 */
typedef struct ccmake_ccm_limits_struct {
    CCUINT64 max_total_bytes;     /* 全部文件内容总字节 */
    CCUINT64 max_file_bytes;      /* 单个文件字节 */

    CCUINT32 max_files;           /* 文件注册表数量 */
    CCUINT32 max_include_edges;   /* include 边总数 */
    CCUINT32 max_include_depth;   /* include 链深度 */

    CCUINT32 max_token_bytes;     /* 单个 token 字节数 */
    CCUINT32 max_array_elements;  /* 单个数组元素数 */
    CCUINT32 max_object_members;  /* 单个对象成员数 */
    CCUINT32 max_value_depth;     /* 值嵌套深度 */

    CCUINT32 max_find_args;       /* ${find} 参数 token 数 */
    CCUINT32 max_find_results;    /* ${find} 结果数 */
    CCUINT32 max_find_depth;      /* ${find} 目录深度 */
} CCMakeCCMLimits;

/* 阶段一上下文：文件表、include 边表、解析顺序与 VFS 探针 */
typedef struct ccmake_ccm_context_struct {
    CCFiles *fs;          /* VFS 探针；ccm_locate 返回前释放 */
    CCDyn    files;       /* CCMakeCCMFile[] */
    CCDyn    includes;    /* CCMakeCCMInclude[] */
    CCDyn    order;       /* CCUINT32[]：后序解析顺序 */
    CCDyn    stack;       /* CCMakeCCMFrame[]：DFS 临时栈 */
    char    *work_dir;    /* 规范化绝对工作目录 */
    CCUINT32 root_index;  /* 根文件在 files 中的下标 */
    CCUINT64 total_bytes; /* 已装载文件内容总字节 */
    CCMakeCCMLimits limits;
} CCMakeCCMContext;

/* 宿主路径类型（VFS 装载前的类型门） */
typedef enum {
    CC_CCM_HOST_MISSING = 0,
    CC_CCM_HOST_FILE,
    CC_CCM_HOST_DIR,
    CC_CCM_HOST_OTHER,
    CC_CCM_HOST_DENIED
} CC_CCM_HOST_TYPE;

/* ------------------------------------------------------------------------- */
/* 阶段一：检查与定位                                                        */
/* ------------------------------------------------------------------------- */

void ccm_context_init(CCMakeCCMContext *ctx);
void ccm_context_clear(CCMakeCCMContext *ctx);

/*
 * 定位根文件与全部 @include 文件，读入内容并生成 files / includes / order。
 * root_path、work_dir 为命令行原值，可相对、可 NULL（work_dir 为 NULL 时取进程 CWD）。
 * 返回时 VFS 已清理，阶段二不再需要文件系统。
 */
CCBOOL ccm_locate(const char *root_path, const char *work_dir, CCMakeCCMContext *ctx);

/* 解析顺序打印（-v 使用） */
void ccm_print_locations(CCMakeCCMContext *ctx);

/* ------------------------------------------------------------------------- */
/* 路径工具                                                                  */
/* ------------------------------------------------------------------------- */

/* 规范化绝对路径；path 相对时以 base 为基准。成功返回 ccalloc 字符串。 */
char *ccm_path_normalize(const char *path, const char *base, const char *comment);

/* 取规范化绝对路径的目录部分；成功返回 ccalloc 字符串。 */
char *ccm_path_dirname(const char *path, const char *comment);

/* 判断路径是否绝对（平台规则） */
CCBOOL ccm_path_is_absolute(const char *path);

/* 判断两个规范化路径是否指向同一文件（Windows 大小写不敏感） */
CCBOOL ccm_path_equal(const char *a, const char *b);

/* 取进程当前工作目录并规范化为绝对路径；失败返回 NULL。 */
char *ccm_path_cwd(const char *comment);

/* 宿主路径类型检查（不跟随到打开文件；用于装载前拒绝目录/特殊文件） */
CC_CCM_HOST_TYPE ccm_host_path_type(const char *path);

/* ------------------------------------------------------------------------- */
/* 词法扫描                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * 扫描 content 中的 @include 指令，追加到 out_includes（CCMakeCCMInclude 数组）。
 * 成功的边项持有 literal，out_includes 的持有者负责最终释放。
 * 只做 include 发现所需的词法处理（注释、字符串、括号深度、行号），
 * 不解析键值语法。display_path 仅用于错误信息。
 */
CCBOOL ccm_scan_includes(
    const char *content,
    CCUINT64 size,
    const char *display_path,
    CCDyn *out_includes,
    CCUINT32 max_edges
);

/* ------------------------------------------------------------------------- */
/* 阶段二：内容解析（后续实现）                                              */
/* ------------------------------------------------------------------------- */

CCBOOL ccm_parse(CCMakeCCMContext *ctx);

/* 第二阶段诊断；返回 CCFALSE，便于 return 传播 */
CCBOOL ccm_parse_failf(
    const char *file,
    CCUINT32 line,
    const char *fmt,
    ...
);

/* 内建函数求值：${ref ...} / ${find ...}
 * 成功时 *out 为新分配字符串，调用方负责 ccalloc(*out, 0, NULL)。 */
CCBOOL ccm_builtin_eval(
    CCMakeCCMContext *ctx,
    const char *raw,
    CCUINT32 len,
    CCVarTree *tree,
    char **out,
    CCUINT32 *out_len,
    const char *file,
    CCUINT32 line
);

/* ${find ...} 求值；args 为已展开内嵌 ${} 的参数串
 * 成功时 *out 为新分配字符串，调用方负责 ccalloc(*out, 0, NULL)。 */
CCBOOL ccm_find_eval(
    CCMakeCCMContext *ctx,
    const char *args,
    CCUINT32 len,
    char **out,
    CCUINT32 *out_len,
    const char *file,
    CCUINT32 line
);

#endif
