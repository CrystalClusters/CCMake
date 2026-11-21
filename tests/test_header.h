/*
 * @Description: CCMake 测试程序内部头文件。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_TEST_HEADER_H_
#define _INCLUDE_TEST_HEADER_H_

#include <color_print.h>

#include "test_harness.h"

/* 进程内测试套件；每个套件内部用 CC_CASE 显式标记用例编号。 */
void test_cmd(void);
void test_ccm_locate(void);
void test_ccm_parse(void);
void test_ccvar(void);

#endif
