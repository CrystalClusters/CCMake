#!/usr/bin/env bash
set -e

# 编译并链接出可执行文件（单二进制）
#
# 用法: bash scripts/build_bin.sh <platform_arch> [build_flags] [dependencies]
#
# 依赖项名称由 build.sh 的 DEPENDENCIES 传入，再交给 scripts/depends.sh：
# 该脚本按名字匹配 depends/ 下的目录（忽略版本号）并输出编译/链接参数，
# -I 开头的是头文件参数，-Wl, 开头的是链接参数。

if [ $# -lt 1 ] || [ -z "$1" ]; then
    echo "[错误] 用法: bash build_bin.sh <platform_arch> [build_flags] [dependencies]"
    exit 2
fi
PFX="$1"
CC_BUILD_CFLAGS="${2:-}"
DEPENDENCIES="${3:-}"

if [ -z "${DEPENDENCIES}" ]; then
    echo "[错误] 依赖项列表为空，请在 build.sh 的 DEPENDENCIES 中填写。"
    exit 1
fi

# 定位项目根并切换（本脚本位于 scripts/ 下）
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "${ROOT}"

OBJ_DIR="out/obj"
BIN_DIR="out/${PFX}_bin"
BIN_NAME="ccmake"
case "${PFX}" in
    windows_*) BIN_NAME="ccmake.exe" ;;
esac

case "${PFX}" in
    *amd64*) CC_BUILD_CFLAGS="${CC_BUILD_CFLAGS} -DCC_ARCH_AMD64" ;;
    *arm64*) CC_BUILD_CFLAGS="${CC_BUILD_CFLAGS} -DCC_ARCH_ARM64" ;;
    *x86*)   CC_BUILD_CFLAGS="${CC_BUILD_CFLAGS} -DCC_ARCH_X86" ;;
    *arm*)   CC_BUILD_CFLAGS="${CC_BUILD_CFLAGS} -DCC_ARCH_ARM" ;;
esac

# 平台相关的额外链接参数
case "${PFX}" in
    windows_*) EXTRA_LIBS="-lws2_32 -liphlpapi" ;;
    *)         EXTRA_LIBS="-pthread" ;;
esac

mkdir -p "${OBJ_DIR}"
mkdir -p "${BIN_DIR}"

# 依赖库平台识别
DEP_FLAGS=$(bash scripts/depends.sh "${PFX}" "${DEPENDENCIES}")
DEP_INC=$(printf '%s\n' "${DEP_FLAGS}" | grep '^-I' || true)
DEP_LIB=$(printf '%s\n' "${DEP_FLAGS}" | grep '^-Wl' || true)
if [ -z "${DEP_LIB}" ]; then
    echo "[错误] 依赖识别没有返回任何链接参数，中止构建。"
    exit 1
fi

# 逐个编译obj
OBJ_LIST=""
SRC_COUNT=0
while IFS= read -r src; do
    [ -n "${src}" ] || continue
    name="${src#./}"             # 去 ./ 前缀
    name="${name%.c}.o"          # 扩展名 .c -> .o
    name="${name//\//_}"         # 路径前缀：/ -> _
    obj="${OBJ_DIR}/${name}"
    echo "编译 ${src} -> ${obj}"
    if ! gcc -c "${src}" -o "${obj}" -Iinclude -Isrc ${DEP_INC} ${CC_BUILD_CFLAGS}; then
        echo "[错误] 编译失败：${src}"
        exit 1
    fi
    OBJ_LIST="${OBJ_LIST} ${obj}"
    SRC_COUNT=$((SRC_COUNT + 1))
done < <(find ./src -name '*.c' | LC_ALL=C sort)

if [ "${SRC_COUNT}" -eq 0 ]; then
    echo "[错误] src/ 下没有找到任何 .c 源文件，请先添加源码。"
    exit 1
fi

# 链接
echo "链接 ${BIN_DIR}/${BIN_NAME}"
if ! gcc ${OBJ_LIST} -o "${BIN_DIR}/${BIN_NAME}" ${CC_BUILD_CFLAGS} ${DEP_LIB} ${EXTRA_LIBS}; then
    echo "[错误] 链接失败：${BIN_DIR}/${BIN_NAME}"
    exit 1
fi
echo "已生成可执行文件：${BIN_DIR}/${BIN_NAME}"
