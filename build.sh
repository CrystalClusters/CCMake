#!/usr/bin/env bash
set -e

# 构建模式：仅显式传入 release 时启用发行构建
BUILD_MODE="DEBUG"
CC_BUILD_CFLAGS="-O0 -g3 -Wall -Wextra"
case "${1:-}" in
    [Rr][Ee][Ll][Ee][Aa][Ss][Ee])
        BUILD_MODE="RELEASE"
        CC_BUILD_CFLAGS="-O2 -DNDEBUG -DCC_RELEASE -Wall -Wextra -Werror"
        ;;
    "")
        ;;
    *)
        echo "[提示] 未知构建模式：$1，按 DEBUG 构建。"
        ;;
esac

# 依赖项列表：按名字在 depends/ 下匹配依赖目录，忽略版本号（CrystalStd 匹配 CrystalStd-v1.0.1）。
# 新增依赖时直接追加，用空格分隔。
DEPENDENCIES="CrystalStd"

# 定位项目根并切换（本脚本位于项目根目录下）
ROOT=$(cd "$(dirname "$0")" && pwd)
cd "${ROOT}"

echo "构建模式：【${BUILD_MODE}】"
echo "编译选项：${CC_BUILD_CFLAGS}"
echo "依赖项：【${DEPENDENCIES}】"

# 平台检测
PLATFORM_STR="$(bash "${ROOT}/scripts/detect.sh")"

echo "当前平台为：【${PLATFORM_STR}】"

# 工具链检测：未通过则中止构建
CHECK="${ROOT}/scripts/check.sh"
if [ ! -f "${CHECK}" ]; then
    echo "[错误] 找不到工具链检测脚本: ${CHECK}"
    exit 1
fi
if ! bash "${CHECK}"; then
    echo "[错误] 工具链检测未通过，中止构建。"
    exit 1
fi

# 生成可执行文件
echo "生成可执行文件..."
bash scripts/build_bin.sh "${PLATFORM_STR}" "${CC_BUILD_CFLAGS}" "${DEPENDENCIES}"
