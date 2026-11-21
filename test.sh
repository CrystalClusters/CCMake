#!/usr/bin/env bash
#
# CCMake 测试运行入口（Unix）
# 测试程序由 build.sh 生成；本脚本只负责运行。
# 用法：./test.sh [all|<套件名>] [--help]

set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "${ROOT}"

PLATFORM_STR="$(bash scripts/detect.sh)"
TEST_EXE="out/${PLATFORM_STR}_bin/ccmake_test"

if [ ! -x "${TEST_EXE}" ]; then
    echo "[错误] 找不到测试程序：${TEST_EXE}"
    echo "[错误] 请先运行 build.sh。"
    exit 2
fi

exec "${TEST_EXE}" "$@"
