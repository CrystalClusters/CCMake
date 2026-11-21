#!/usr/bin/env bash
set -e

# 编译并链接 CCMake 测试程序（独立可执行文件，链接 src/cmd.c）
#
# 用法: bash scripts/build_test.sh <platform_arch> [build_flags] [dependencies]
#
# 依赖项名称交给 scripts/depends.sh 识别，-I 开头的是头文件参数，
# -Wl, 开头的是链接参数。

if [ $# -lt 1 ] || [ -z "$1" ]; then
    echo "[错误] 用法: bash build_test.sh <platform_arch> [build_flags] [dependencies]"
    exit 2
fi
PFX="$1"
CC_BUILD_CFLAGS="${2:-}"
DEPENDENCIES="${3:-CrystalStd}"

if [ -z "${DEPENDENCIES}" ]; then
    echo "[错误] 依赖项列表为空。"
    exit 1
fi

# 定位项目根并切换（本脚本位于 scripts/ 下）
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "${ROOT}"

BIN_DIR="out/${PFX}_bin"
BIN_NAME="ccmake_test"
case "${PFX}" in
    windows_*) BIN_NAME="ccmake_test.exe" ;;
esac

mkdir -p "${BIN_DIR}"

# 依赖库平台识别
DEP_FLAGS=$(bash scripts/depends.sh "${PFX}" "${DEPENDENCIES}")
DEP_INC=$(printf '%s\n' "${DEP_FLAGS}" | grep '^-I' || true)
DEP_LIB=$(printf '%s\n' "${DEP_FLAGS}" | grep '^-Wl' || true)
if [ -z "${DEP_LIB}" ]; then
    echo "[错误] 依赖识别没有返回任何链接参数，中止构建。"
    exit 1
fi

# 收集测试源码与应用源码（测试程序不链接 main.c）
SRCS=$(find tests -name '*.c' | LC_ALL=C sort)
APP_SRCS=$(find src -name '*.c' ! -name 'main.c' | LC_ALL=C sort)
if [ -z "${SRCS}" ]; then
    echo "[错误] tests/ 下没有找到测试源文件。"
    exit 1
fi
if [ -z "${APP_SRCS}" ]; then
    echo "[错误] src/ 下没有找到应用源文件。"
    exit 1
fi

echo "编译测试程序..."
# shellcheck disable=SC2086
if ! gcc ${SRCS} ${APP_SRCS} -o "${BIN_DIR}/${BIN_NAME}" \
        -Itests -Iinclude -Isrc ${DEP_INC} ${CC_BUILD_CFLAGS} ${DEP_LIB} -pthread; then
    echo "[错误] 编译测试程序失败。"
    exit 1
fi
echo "已生成测试程序：${BIN_DIR}/${BIN_NAME}"
