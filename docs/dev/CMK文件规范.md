# CMK 文件规范

## 概述

`.cmk` 文件是 CCM 语言的**逻辑执行层**，用于组织构建流程——可以定义变量、编写控制流、封装函数，以及执行外部命令。语法**类似 C 语言**，无隐式行为。

与 `.ccm`（数据层）的关系：

```
    .ccm                .cmk
Build Context       Build Procedure
   构建环境             构建流程
      |                   |
      +------- ccmake ----+
           脚本解释执行器
                |
            操作系统
```

### 设计理念

| 设计 | 说明 |
|----------|------|
| C 风格语法 | 分号结尾、大括号块、经典控制流 |
| 无隐式 | 绝对没有小动作 |
| 图灵完备 | 变量、循环、分支、函数，可描述任意复杂流程 |
| 精准导入 | `@import` 按接口从其他 `.cmk` 引入，避免命名污染 |
| 返回值必处理 | 有返回值的函数的返回值必须赋值给变量或用 `_` 显式丢弃 |

---

## 快速入门

最小可用的 `.cmk`：

```c
@include "config/project.ccm";

func build() {
    string output = @{output};
    int ret = exec(@{cc} @{cflags} -o @{output} @{sources});
    if (ret != 0) error("编译失败");
}

entry build;
```

搭配 `.ccm` 配置：

```
"cc": "gcc";
"cflags": "-O2";
"output": "./hello";
"sources": ${find ./src -include *.c};
```

执行：

```bash
ccmake -c project.ccm -f build.cmk        # 执行 entry 函数
ccmake -c project.ccm -f build.cmk -t clean  # 执行 clean 函数
```

---

## 基本语法

### 语句

每条语句以分号 `;` 结束。代码块使用 `{}` 包裹。

### 注释

**行注释：** `//` 开头，到行尾结束。

**块注释：** `/*` 开头，`*/` 结尾，可跨多行。

```c
// 这是单行注释
int x = 1;

/*
 * 多行注释
 * 可跨越多行
 */
int y = 2;
```

### 标识符

标识符由字母、数字和下划线组成，不能以数字开头。大小写敏感。

### 关键字

| | | | |
|---|---|---|---|
| `string` | `int` | `bool` | `float` |
| `list` | `file_stat` | `const` | `if` |
| `else` | `for` | `in` | `while` |
| `break` | `continue` | `func` | `return` |
| `entry` | `@include` | `@import` | `from` |
| `as` | `true` | `false` | `_` |

---

## 变量与类型

### 类型一览

| 类型 | 说明 | 示例 |
|------|------|------|
| `string` | 字符串 | `"hello"` |
| `int` | 有符号整数 | `42` |
| `float` | 浮点数 | `3.14` |
| `bool` | 布尔值 | `true` / `false` |
| `list` | 有序值列表 | `["a", "b", "c"]` |
| `file_stat` | 文件状态（由 `stat()` 返回） | `stat("main.c")` |

### 变量声明

```c
string  compiler = "gcc";
int     jobs     = 8;
bool    verbose  = false;
float   scale    = 2.5;
list    files    = ["main.c", "util.c"];
```

### 从 CCM 取值

使用 `@{字段名}` 语法从 `.ccm` 配置中读取字段值，支持 `.` 层级寻址和数组索引：

```c
string cc      = @{toolchain.c_compiler};   // "gcc"
string flags   = @{cflags};                 // "-O2"
string first   = @{defines[0]};             // "CC_USE_VULKAN"
```

> `@{...}` 取值时保留字段在 `.ccm` 中定义的类型（数字即数字，列表即列表）。

### 运算符

#### 算术

| 运算符 | 说明 |
|--------|------|
| `+` | 加 / 字符串拼接 |
| `-` | 减 |
| `*` | 乘 |
| `/` | 除 |
| `%` | 取模 |

#### 赋值

| 运算符 | 说明 |
|--------|------|
| `=` | 赋值 |
| `+=` | 加后赋值（列表追加、数值累加） |

#### 比较

| 运算符 | 说明 |
|--------|------|
| `==` | 等于 |
| `!=` | 不等于 |
| `<` | 小于 |
| `>` | 大于 |
| `<=` | 小于等于 |
| `>=` | 大于等于 |

#### 逻辑

| 运算符 | 说明 |
|--------|------|
| `&&` | 逻辑与 |
| `\|\|` | 逻辑或 |
| `!` | 逻辑非 |

### 列表操作

```c
list sources = ["a.c", "b.c"];

// 追加
sources += "c.c";                    // ["a.c", "b.c", "c.c"]

// 索引访问
string first = sources[0];           // "a.c"

// 拼接为字符串
string all = join(sources, ",");     // "a.c,b.c,c.c"

// 长度
int n = len(sources);                // 3
```

---

## 文件导入

### 导入 CCM 数据

使用 `@include` 导入 `.ccm` 配置文件，使其所有字段可被当前 `.cmk` 通过 `@{...}` 引用：

```c
@include "config/linux.ccm";
@include "config/common.ccm";
```

### 导入 CMK 接口

使用 `@import` 从其他 `.cmk` 文件按需引入函数。不支持整文件包含。

```c
// 导入函数
@import func compile   from "rules/compile.cmk";
@import func link_all  from "rules/link.cmk";

// 批量导入
@import func compile, link from "rules/common.cmk";
@import func * from "utils/helpers.cmk";       // 导入所有函数
```

#### 别名

当导入的接口与本地名称冲突时，使用 `as` 重命名：

```c
@import func compile as gcc_compile from "rules/gcc.cmk";
@import func compile as msvc_compile from "rules/msvc.cmk";
```

---

## 控制流

### if 语句

```c
if (@{platform} == "linux") {
    string cc = "gcc";
} else if (@{platform} == "windows") {
    string cc = "clang";
} else {
    error("不支持的平台: %s", @{platform});
}
```

条件可以是任意比较表达式或布尔值：

```c
if (verbose && jobs > 4) {
    info("详细模式，并行任务数: %d", jobs);
}
```

### for-in 循环

遍历列表：

```c
list files = @{c_files};
for (string src in files) {
    info("处理文件: %s", src);
}
```

### while 循环

```c
int retry = 3;
while (retry > 0) {
    int ret = exec(make test);
    if (ret == 0) break;
    retry = retry - 1;
}
```

---

## 函数与入口

### 函数定义

```c
func 函数名(参数列表) -> 返回类型 {
    函数体
}
```

### 参数与返回值

```c
// 带返回值的函数
func compile(string src) -> string {
    string obj = subst(src, ".c", ".o");
    int ret = exec(@{cc} @{cflags} -c @{src} -o @{obj});
    if (ret != 0) error("编译失败: %s", src);
    return obj;
}

// 无返回值函数
func clean() {
    _ = exec(rm -rf ./build);
}
```

### 调用

```c
string obj = compile("main.c");
clean();
```

### 作用域

函数内部声明的变量为局部变量，外部不可见。函数可以访问全局变量和 `@{...}` 配置值。

### 入口声明

`entry` 声明当命令行未通过 `-t` 指定目标时，默认执行的函数：

```c
func build() {
    // 主构建逻辑
}

func clean() {
    _ = exec(rm -rf ./build);
}

entry build;   // 默认入口
```

命令行使用：

```bash
ccmake -c config.ccm -f build.cmk           # 执行 entry 函数（build）
ccmake -c config.ccm -f build.cmk -t clean  # 执行 clean 函数
```

每个 `.cmk` 最多有一个 `entry` 声明。如果 `-t` 指定了目标但 `.cmk` 中不存在同名函数，CCMake 报错退出。

---

## 文件状态与增量构建

### stat() 函数

`stat(path)` 返回一个 `file_stat` 对象，用于显式判断文件状态。CCMake 内部维护文件元信息，不依赖系统时间戳的可靠性问题。

```c
file_stat s = stat("main.c");
```

### file_stat 类型字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `.exists` | `bool` | 文件是否存在 |
| `.size` | `int` | 文件大小（字节） |
| `.mtime` | `int` | 最后修改时间（Unix 时间戳） |
| `.hash` | `string` | 文件内容哈希（CCMake 维护，用于精确变更检测） |
| `.is_newer_than(file_stat other)` | `bool` | 当前文件是否比 other 更新（比较 hash，hash 相同时退化为 mtime） |

### 显式增量构建

由用户自己编写判断逻辑，增量行为完全透明：

```c
func build() {
    list objs = [];
    for (string src in @{sources}) {
        string obj = subst(src, ".c", ".o");

        file_stat s = stat(src);
        file_stat o = stat(obj);

        // 显式判断是否需要重新编译
        if (!o.exists || s.is_newer_than(o)) {
            int ret = exec(@{cc} @{cflags} -c @{src} -o @{obj});
            if (ret != 0) error("编译失败: %s", src);
        } else {
            info("跳过: %s (未变更)", src);
        }

        objs += obj;
    }
    int ret = exec(@{linker} @{objs} -o @{output});
    if (ret != 0) error("链接失败");
}
```

### 构建缓存

`cache_get(key)` 和 `cache_set(key, val)` 使用 CCMake 维护的持久化键值缓存，跨次运行保留状态：

```c
// 记录上次构建的提交哈希
string last_commit = cache_get("last_commit");
string current = exec_capture(git rev-parse HEAD);

if (last_commit != current) {
    info("检测到新提交，全量重建");
    full_rebuild();
    cache_set("last_commit", current);
} else {
    incremental_build();
}
```

缓存由 CCMake 自动持久化到构建目录下的 `.ccmake_cache` 文件中。

---

## 返回值规则（强制）

**所有有返回值的函数调用，其返回值必须显式赋给变量，否则用 `_` 丢弃。编译器对未处理的返回值报错。**

这适用于所有返回非 void 的函数——无论是内置函数、外部命令还是用户自定义函数：

```c
int ret = exec(gcc -c main.c);        // ✅ 赋值给变量
_ = exec(mkdir -p ./build);           // ✅ 显式丢弃
exec(gcc -c main.c);                  // ❌ 编译错误：返回值未处理

int n = len(sources);                 // ✅ 内置函数返回值必须处理
len(sources);                         // ❌ 编译错误：返回值未处理

string obj = compile("main.c");       // ✅ 用户函数返回值必须处理
compile("main.c");                    // ❌ 编译错误：返回值未处理
```

> 唯一例外：`info`、`warn`、`error`、`cache_set` 无返回值，可不赋值直接调用。

---

## 外部命令执行

### exec — 执行命令，返回退出码

```c
int ret = exec(gcc -c main.c -o main.o);
if (ret != 0) {
    error("编译失败，退出码: %d", ret);
}
```

stdout 和 stderr 正常输出到终端。

### exec_capture — 捕获 stdout

```c
string ver = exec_capture(gcc --version);
info("编译器版本: %s", ver);
```

stderr 正常输出到终端。

---

## 内置函数库

### 字符串操作

| 函数 | 签名 | 说明 |
|------|------|------|
| `subst` | `subst(string, from, to) -> string` | 将字符串中所有 `from` 替换为 `to` |
| `basename` | `basename(path) -> string` | 提取路径的文件名部分 |
| `dirname` | `dirname(path) -> string` | 提取路径的目录部分 |
| `join` | `join(list, sep) -> string` | 将列表以分隔符拼接为字符串 |
| `len` | `len(list) -> int` | 返回列表元素个数 |

```c
string obj  = subst("src/main.c", ".c", ".o");   // "src/main.o"
string name = basename("/path/to/file.c");        // "file.c"
string dir  = dirname("/path/to/file.c");         // "/path/to"
string all  = join(["a", "b", "c"], ", ");        // "a, b, c"
int    n    = len(sources);                        // 3
```

### 文件系统

| 函数 | 签名 | 说明 |
|------|------|------|
| `stat` | `stat(path) -> file_stat` | 获取文件状态对象 |
| `cache_get` | `cache_get(key) -> string` | 从构建缓存读取值 |
| `cache_set` | `cache_set(key, val)` | 写入构建缓存 |

### 环境

| 函数 | 签名 | 说明 |
|------|------|------|
| `env` | `env(name) -> string` | 读取系统环境变量 |

```c
string home = env("HOME");
string path = env("PATH");
```

### 日志

| 函数 | 签名 | 说明 |
|------|------|------|
| `info` | `info(fmt, args...)` | 输出信息日志 |
| `warn` | `warn(fmt, args...)` | 输出警告日志 |
| `error` | `error(fmt, args...)` | 输出错误日志并中止构建 |

```c
info("开始构建: %s", target_name);
warn("未找到缓存，将全量构建");
error("配置文件缺失: %s", config_path);
```

---

## 完整示例

### 示例 1：单文件 C 项目

**project.ccm**

```
"cc": "gcc";
"cflags": "-O2 -Wall";
"output": "./build/hello";
"sources": ${find ./src -include *.c};
```

**build.cmk**

```c
@include "project.ccm";

func compile(string src) -> string {
    string obj = subst(src, ".c", ".o");

    file_stat s = stat(src);
    file_stat o = stat(obj);

    if (!o.exists || s.is_newer_than(o)) {
        info("编译: %s -> %s", src, obj);
        int ret = exec(@{cc} @{cflags} -c @{src} -o @{obj});
        if (ret != 0) error("编译失败: %s", src);
    } else {
        info("跳过: %s (未变更)", src);
    }

    return obj;
}

func build() {
    list objs = [];
    for (string src in @{sources}) {
        objs += compile(src);
    }

    int ret = exec(@{cc} @{objs} -o @{output});
    if (ret != 0) error("链接失败");

    info("构建完成: %s", @{output});
}

func clean() {
    _ = exec(rm -rf ./build);
}

entry build;
```

### 示例 2：多平台项目 + 构建缓存

**rules/compile.cmk**（可复用编译逻辑）

```c
func compile_c(string src, string obj) -> bool {
    file_stat s = stat(src);
    file_stat o = stat(obj);

    if (!o.exists || s.is_newer_than(o)) {
        info("编译: %s", src);
        int ret = exec(@{toolchain.cc} @{cflags} -c @{src} -o @{obj});
        if (ret != 0) {
            error("编译失败: %s", src);
            return false;
        }
        return true;
    }

    info("跳过: %s (未变更)", src);
    return true;
}

func obj_path(string src) -> string {
    return @{build_dir} + "/" + basename(subst(src, ".", "_")) + ".o";
}
```

**build.cmk**

```c
@include "config/linux.ccm";

@import func compile_c from "rules/compile.cmk";
@import func obj_path  from "rules/compile.cmk";

string build_dir = "./build";

func build() {
    // 确保输出目录存在
    _ = exec(mkdir -p @{build_dir});

    list objs = [];
    for (string src in @{sources}) {
        string obj = obj_path(src);
        if (compile_c(src, obj)) {
            objs += obj;
        }
    }

    int ret = exec(@{toolchain.linker} @{objs} -o @{build_dir}/@{project} -l@{libs});
    if (ret != 0) error("链接失败");

    info("构建成功: %s/%s", @{build_dir}, @{project});
}

func clean() {
    _ = exec(rm -rf @{build_dir});
    info("清理完成");
}

// 结合构建缓存的全量/增量开关
func smart_build() {
    string last_hash = cache_get("src_hash");

    // 简单哈希判断（用户可自定义更精细的策略）
    string current_hash = exec_capture(cat @{sources} | md5sum);

    if (last_hash != current_hash) {
        clean();
        build();
        cache_set("src_hash", current_hash);
    } else {
        info("源码未变更，跳过构建");
    }
}

entry smart_build;
```

### 示例 3：自定义自动化任务

```c
@include "config/release.ccm";

func package() {
    _ = exec(mkdir -p @{package_dir});

    for (string asset in @{assets}) {
        string dest = @{package_dir} + "/" + basename(asset);
        int ret = exec(cp @{asset} @{dest});
        if (ret != 0) warn("打包失败: %s", asset);
    }

    int ret = exec(tar -czf @{package_name}.tar.gz -C @{package_dir} .);
    if (ret != 0) error("打包失败");
    info("打包完成: %s.tar.gz", @{package_name});
}

func deploy() {
    int ret = exec(scp @{package_name}.tar.gz user@server:/deploy/);
    if (ret != 0) error("上传失败");

    ret = exec(ssh user@server "cd /deploy && tar -xzf @{package_name}.tar.gz");
    if (ret != 0) error("部署失败");

    info("部署完成");
}

entry package;
```

---

## 附录

### 与 CCM 的语法对照

| 特性 | `.ccm` | `.cmk` |
|------|--------|--------|
| 核心职责 | 声明数据 | 执行逻辑 |
| 值引用 | `${ref key}`（内部） | `@{key}`（取 CCM 值） |
| 文件导入 | `@include "x.ccm"` | `@include "x.ccm"` + `@import func from "x.cmk"` |
| 语句结尾 | `;` | `;` |
| 块结构 | `{}` 嵌套数据 | `{}` 代码块 |
| 注释 | `//` + `/* */` | `//` + `/* */` |
| 类型 | 自动推断 | 显式声明 |
| 控制流 | 无 | `if` / `for` / `while` |
| 外部命令 | 不可用 | `exec` / `exec_capture`（返回值必须处理） |
| 增量构建 | — | `stat()` + `if` 显式判断 |

### 关键字速查

| 关键字 | 用途 |
|--------|------|
| `string` | 字符串类型 |
| `int` | 整数类型 |
| `bool` | 布尔类型 |
| `float` | 浮点数类型 |
| `list` | 列表类型 |
| `file_stat` | 文件状态类型（`stat()` 返回值） |
| `const` | 常量修饰 |
| `if` / `else` | 条件分支 |
| `for` / `in` | 遍历循环 |
| `while` | 条件循环 |
| `break` / `continue` | 循环控制 |
| `func` / `return` | 函数定义与返回 |
| `entry` | 默认入口函数声明 |
| `@include` | 导入 CCM 数据 |
| `@import` | 导入 CMK 函数接口 |
| `from` / `as` | 导入修饰子句 |
| `true` / `false` | 布尔字面量 |
| `_` | 显式丢弃返回值 |

### 常见问题

**Q：为什么所有有返回值的函数返回值都必须处理？**

防止遗忘错误处理。构建流程中忽略返回值是常见的 bug 来源——无论是忽略命令退出码还是丢弃函数的计算结果。如果真的不关心结果，显式写 `_ = func(...)` 表明这是有意为之。

**Q：增量构建怎么做？**

使用 `stat()` 获取源文件和目标文件的状态，通过 `if` 显式判断是否需要重新编译。也可以用 `cache_get/set` 实现更复杂的增量策略。

**Q：`file_stat.is_newer_than()` 比较的是什么？**

优先比较内容哈希（CCMake 内部维护），哈希相同时退化为比较 `mtime`。这比单纯依赖文件系统时间戳更可靠。

**Q：怎样复用其他 `.cmk` 中的函数？**

```c
@import func compile from "rules/compile.cmk";
```

只导入你需要的函数，不会引入被导入文件的其他副作用。