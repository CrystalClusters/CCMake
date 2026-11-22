/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: .cmk 源码缓冲区与源码位置
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CMK_SOURCE_H_
#define _INCLUDE_CMK_SOURCE_H_

#include <crystal_cluster.h>

/* 源码位置：file 为借用指针，行号与字节列号均从 1 开始 */
typedef struct cmk_location {
    const char *file;
    CCUINT32 line;
    CCUINT32 col;
} CMKLocation;

/* 一段源码范围；end_line/end_col 指向最后一个字节（包含） */
typedef struct cmk_source_span {
    CCUINT64 offset;   /* 起始字节偏移 */
    CCUINT64 width;    /* 覆盖的字节数 */
    CCUINT32 line;     /* 起始行，1 基 */
    CCUINT32 col;      /* 起始字节列，1 基 */
    CCUINT32 end_line; /* 最后一个字节所在行，1 基 */
    CCUINT32 end_col;  /* 最后一个字节所在列，1 基（包含） */
} CMKSourceSpan;

/* 一份已装载的 .cmk 源码；path 与 data 都由本结构持有，data 末尾补 0 */
typedef struct cmk_source {
    char *path;
    char *data;
    CCUINT64 size;
} CMKSource;

void cmk_source_init(CMKSource *source);
void cmk_source_clear(CMKSource *source);

/*
 * 用调用方的 path / data 副本重建源码缓冲区，旧内容先释放。
 * data 只读，size 为有效字节数；内部复制并追加结尾 0，不接管调用方指针。
 * path 与 data 都不能为 NULL（data 在 size 为 0 时可传 NULL）。
 * size 必须满足 size <= SIZE_MAX - 1；超出时返回 CCFALSE，旧内容保持不变。
 */
CCBOOL cmk_source_set(
    CMKSource *source, const char *path,
    const char *data, CCUINT64 size
);

#endif
