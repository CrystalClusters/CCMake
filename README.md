# CCMake
Crystal Cluster Make，水晶簇的内部项目管理器

## 目录结构

```
depends/     外部依赖
include/     公开头文件
scripts/     各平台细分构建脚本
src/         源码
out/         构建产物
```

## 构建

|平台|脚本|
|---|---|
|Linux 及其他 Unix Like|`build.sh`|
|Windows|`build.bat`|

默认执行 DEBUG 构建；只有显式传入 `release` 时才启用 RELEASE 构建：

```bash
./build.sh          # DEBUG
./build.sh release  # RELEASE
```

```bat
build.bat
build.bat release
```

DEBUG 使用 `-O0 -g3 -Wall -Wextra`，RELEASE 使用 `-O2 -DNDEBUG -DCC_RELEASE -Wall -Wextra -Werror`；Windows 两种模式均额外追加 `-fexec-charset=GBK`。编译选项由顶层脚本统一传给 `scripts/build_*.sh` / `.bat` 子脚本。

**产物：**

```
out/
├── obj/                  中间目标文件（.o）
└── <平台>_bin/           按平台架构命名的产物目录
    └── ccmaker(.exe)     可执行文件
```

## 依赖

`CrystalStd`基础库（水晶簇体系内标准基础库）。

链接静态库时填加 `-Wl,--whole-archive ... -Wl,--no-whole-archive`：CrystalStd 的自动初始化/逆初始化不产生外部引用，不全量归档会被归档抽取规则丢弃（见`CrystalStd`文档）。
