#!/usr/bin/env bash
set -e

# 依赖库平台识别

# 依赖列表位于 build.bat 内 DEPENDENCIES 字段（可追加）

if [ $# -lt 2 ] || [ -z "$1" ] || [ -z "$2" ]; then
    echo "[错误] 用法: bash depends.sh <platform_arch> \"<依赖项名称列表>\"" >&2
    exit 2
fi
PFX="$1"
DEPS="$2"

# 定位项目根并切换（本脚本位于 scripts/ 下）
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "${ROOT}"

if [ ! -d "depends" ]; then
    echo "[错误] 找不到依赖目录: depends" >&2
    exit 1
fi

INC_FLAGS=()
LIB_FLAGS=()
ERR_COUNT=0

# 报告一个问题（继续处理后面的依赖，最后统一判定）
report_error() {
    echo "[错误] $1" >&2
    ERR_COUNT=$((ERR_COUNT + 1))
}

for name in ${DEPS}; do
    # 忽略版本号匹配目录：<名字> 或 <名字>-v<版本>
    pkg=""
    cands=""
    matches=0
    for cand in "depends/${name}" "depends/${name}"-v*/; do
        [ -d "${cand}" ] || continue
        pkg="${cand%/}"
        cands="${cands} ${pkg##*/}"
        matches=$((matches + 1))
    done

    if [ "${matches}" -eq 0 ]; then
        report_error "找不到依赖 ${name}：depends/ 下没有 ${name} 或 ${name}-v<版本> 目录"
        continue
    fi
    if [ "${matches}" -gt 1 ]; then
        report_error "依赖 ${name} 匹配到多个目录，无法确定用哪一个：${cands# }"
        continue
    fi

    # 头文件目录可选
    if [ -d "${pkg}/include" ]; then
        INC_FLAGS+=("-I${pkg}/include")
    fi

    libdir="${pkg}/${PFX}_bin"
    if [ ! -d "${libdir}" ]; then
        report_error "依赖 ${pkg##*/} 缺少当前平台（${PFX}）的库目录：${libdir}"
        continue
    fi

    libnum=0
    for lib in "${libdir}"/*.a; do
        [ -f "${lib}" ] || continue
        # CrystalStd 一类的自动初始化/逆初始化不产生外部引用，必须全量归档
        LIB_FLAGS+=("-Wl,--whole-archive ${lib} -Wl,--no-whole-archive")
        libnum=$((libnum + 1))
    done
    if [ "${libnum}" -eq 0 ]; then
        report_error "依赖 ${pkg##*/} 的平台目录里没有静态库（.a）：${libdir}"
        continue
    fi

    echo "[依赖] ${pkg}" >&2
done

if [ "${ERR_COUNT}" -ne 0 ]; then
    echo "[错误] 依赖识别失败，共 ${ERR_COUNT} 项问题，未继续构建。" >&2
    exit 1
fi

if [ "${#LIB_FLAGS[@]}" -eq 0 ]; then
    echo "[错误] 没有识别到任何可链接的依赖库" >&2
    exit 1
fi

for flag in "${INC_FLAGS[@]}"; do
    echo "${flag}"
done
for flag in "${LIB_FLAGS[@]}"; do
    echo "${flag}"
done
