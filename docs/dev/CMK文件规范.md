# CMK 文件规范

## 概述

`.cmk` 文件是 CCM 语言的**逻辑执行层**，用于组织构建流程——可以定义变量、编写控制流、封装函数，以及执行外部命令。语法**类似 C 语言**，无隐式行为。

> 构建目标、IR、Scheduler 与 Executor 的架构见 [CMK实现细则](CMK实现细则.md)。Configure 阶段不执行外部命令，平台与工具链信息可由 `.ccm` 配置变量传入，也可通过只读内建查询；目标模式使用 `target` 声明，默认目标为 `main()`。

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

最小目标模式的 `.cmk`：

```c
@include "config/project.ccm";

target main() {
    list<target_ref> depends_on = {app};
}

target app() {
    list<string> outputs = {"hello"};
    list<action> actions = {
        command(@{cc}, {@{cflags}, "-o", "hello"} + @{sources})
    };
}
```

搭配 `.ccm` 配置：

```text
"cc": "gcc";
"cflags": "-O2";
"output": "./hello";
"sources": ${find ./src -include *.c};
```

执行：

```bash
ccmake -c project.ccm -f build.cmk        # 构建默认目标 main
ccmake -c project.ccm -f build.cmk -t app # 构建指定目标 app
```

> 上面的 `command(@{cc}, {...})` 中，`@{cc}` 是单值配置读取；`@{sources}` 是列表。`command` 的参数列表使用 C 风格花括号。

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
| `list` | `action` | `target` | `target_ref` |
| `const` | `if` | `else` | `for` |
| `in` | `while` | `break` | `continue` |
| `func` | `return` | `@include` | `@import` |
| `from` | `as` | `true` | `false` |
| `void` | `_` | | |

---

## 变量与类型

### 类型一览

| 类型 | 说明 | 示例 |
|------|------|------|
| `string` | 字符串 | `"hello"` |
| `int` | 有符号整数 | `42` |
| `float` | 浮点数 | `3.14` |
| `bool` | 布尔值 | `true` / `false` |
| `list<T>` | 同质列表，T 为元素类型 | `list<string> files = {"a", "b"}` |
| `action` | 目标动作 | `command("cc", {...})` |
| `target_ref` | 静态目标引用，仅用于 `depends_on` | `core`、`common.utils` |

### 变量声明

```c
string  compiler = "gcc";
int     jobs     = 8;
bool    verbose  = false;
float   scale    = 2.5;
list<string> files = {"main.c", "util.c"};
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
list<string> sources = {"a.c", "b.c"};

// 追加
sources += "c.c";                    // {"a.c", "b.c", "c.c"}

// 索引访问
string first = sources[0];           // "a.c"

// 拼接为字符串
string all = join(sources, ",");     // "a.c,b.c,c.c"

// 长度
int n = len(sources);                // 3
```

- `list<T>` 是同质列表，元素类型必须显式声明；
- 空列表 `{}` 的元素类型由声明、函数返回类型或目标字段上下文确定；
- `+` 连接两个元素类型相同的列表；
- `+=` 只追加一个元素，元素类型必须匹配；
- 索引从 0 开始，越界在运行时报告错误；
- 不支持嵌套列表；
- `.ccm` 数组映射为同质 `list<T>`，空数组需要上下文类型，混合或嵌套数组报错。

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

当导入的函数与本地名称冲突时，使用 `as` 重命名：

```c
@import func compile as gcc_compile from "rules/gcc.cmk";
@import func compile as msvc_compile from "rules/msvc.cmk";
```

#### 目标导入

函数导入与目标导入是独立语义。目标必须显式列出并绑定模块别名：

```c
@import target core, utils from "targets/common.cmk" as common;
```

目标通过 `common.core`、`common.utils` 引用；函数导入不会暴露目标，目标导入也不会把函数并入当前函数命名空间。模块级变量和私有函数只在该模块内部可见，完整规则见 [CMK实现细则](CMK实现细则.md)。

模块级变量不跨模块导出；其他模块只能通过导入的函数间接使用。

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
list<string> files = @{c_files};
for (string src in files) {
    info("处理文件: %s", src);
}
```

### while 循环

```c
int remaining = 3;
while (remaining > 0) {
    remaining = remaining - 1;
}
```

---

## 函数

### 函数定义

```c
func 函数名(参数列表) -> 返回类型 {
    函数体
}
```

返回类型必须显式声明。无返回值写 `-> void`，省略返回类型是语义错误。

```c
func print_info(string message) -> void {
    info("%s", message);
}
```

### 参数与返回值

```c
func object_name(string src) -> string {
    return subst(src, ".c", ".o");
}

func release_flags() -> list<string> {
    return {"-O2", "-DNDEBUG"};
}
```

`return;` 只能用于 `-> void` 函数；非 void 函数的所有控制流路径都必须返回值。函数可以返回 `action` 或 `list<action>`，用于构造目标动作。

`error()` 的类型是 `-> void`，但属于不返回函数：调用后终止 Configure，不生成可执行 Plan。

### 调用

```c
string obj = object_name("main.c");
list<string> flags = release_flags();
```

### 作用域

函数内部声明的变量为局部变量，外部不可见。函数可以访问所属模块的模块级变量和 `@{...}` 配置值。

### 入口

构建模式默认选择目标 `main()`。`-t name` 选择其他目标并构建其依赖闭包。目标声明与选择规则见 [CMK实现细则](CMK实现细则.md)。

---

## 返回值规则（强制）

**所有有返回值的函数调用，其返回值必须显式赋给变量，否则用 `_` 丢弃。编译器对未处理的返回值报错。**

这适用于所有返回非 void 的函数——包括内置函数和用户自定义函数：

```c
int n = len(sources);                 // ✅ 内置函数返回值必须处理
len(sources);                         // ❌ 编译错误：返回值未处理

string obj = object_name("main.c");   // ✅ 用户函数返回值必须处理
object_name("main.c");                // ❌ 编译错误：返回值未处理
```

> 唯一例外：`info`、`warn`、`error` 无返回值，可不赋值直接调用。

---

## 命令执行边界

Configure 阶段**不执行外部命令**。构建目标平台、工具链、编译器等信息通过 `.ccm` 配置变量传入；当前执行主机的系统信息通过只读内建 `host_info` 查询。两者不隐式互相覆盖，交叉编译时分别保存：

```text
"platform": "windows";
"toolchain": {
    "c": "cl"
};
```

```c
string target_platform = @{platform};
string host_os = host_info("os");
string cc = @{toolchain.c};
```

### 目标动作

外部命令只能作为目标动作，在 Build 阶段由 Executor 执行：

```c
target app() {
    list<action> actions = {
        mkdir("build"),
        command("cc", {"-O2", "-c", "main.c", "-o", "main.o"}),
        copy("main.o", "build/main.o")
    };
}
```

- `command` 接收 program + args 列表，不经过 shell；
- `args` 使用 C 风格花括号列表；
- `mkdir` / `copy` / `move` / `remove` 执行文件动作；
- 动作参数在 Configure 阶段求值，执行发生在 Build 阶段；
- 需要 Shell 语义时显式调用 `sh -c` / `cmd /c`，由脚本自行承担风险。

Build 阶段的进程失败和文件动作失败不经过 `error()`，而是由 Scheduler 标记目标 `FAILED` 并传播到依赖目标。

---

## 内置函数库

内建函数与用户函数使用相同的调用语法；统一签名、注册和扩展接口见 [CMK实现细则](CMK实现细则.md)。

### 文件与系统查询

| 函数 | 签名 | 说明 |
|------|------|------|
| `find_files` | `find_files(pattern, roots) -> list<string>` | 在只读根目录中搜索匹配文件 |
| `host_info` | `host_info(key) -> string` | 查询只读系统信息：`os`、`arch`、`cpu_count`、`path_separator` |

```c
list<string> sources = find_files("*.c", {"src", "tests"});
string os = host_info("os");
string cpus = host_info("cpu_count");
```

`find_files` 只读、结果去重并排序；`host_info` 不接受任意键，不创建进程。

### 字符串操作

| 函数 | 签名 | 说明 |
|------|------|------|
| `subst` | `subst(string, from, to) -> string` | 将字符串中所有 `from` 替换为 `to` |
| `basename` | `basename(path) -> string` | 提取路径的文件名部分 |
| `dirname` | `dirname(path) -> string` | 提取路径的目录部分 |
| `join` | `join(list<string>, sep) -> string` | 将列表以分隔符拼接为字符串 |
| `len` | `len(list<T>) -> int` | 返回列表元素个数 |

```c
string obj  = subst("src/main.c", ".c", ".o");   // "src/main.o"
string name = basename("/path/to/file.c");       // "file.c"
string dir  = dirname("/path/to/file.c");        // "/path/to"
string all  = join({"a", "b", "c"}, ", ");       // "a, b, c"
int    n    = len(sources);                      // 3
```

### 日志

| 函数 | 签名 | 说明 |
|------|------|------|
| `info` | `info(fmt, args...)` | 输出信息日志 |
| `warn` | `warn(fmt, args...)` | 输出警告日志 |
| `error` | `error(fmt, args...) -> void` | 输出错误诊断并终止 Configure；不生成可执行 Plan |

`info`、`warn`、`error` 都是 `-> void`。`error` 标记为不返回：调用后当前 Configure 控制流结束，不会进入 Build。

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
"platform": "windows";
"toolchain": {
    "c": "cl"
};
"cflags": {"-O2", "-W4"};
"output": "./build/hello.exe";
"sources": ${find ./src -include *.c};
```

**build.cmk**

```c
@include "project.ccm";

func object_of(string src) -> string {
    return subst(src, ".c", ".obj");
}

func compile_actions() -> list<action> {
    list<action> actions = {mkdir("build")};
    for (string src in @{sources}) {
        actions += command(@{toolchain.c}, {@{cflags}, "/c", src, "/Fo" + object_of(src)});
    }
    return actions;
}

func link_actions() -> list<action> {
    list<string> args = {};
    for (string src in @{sources}) {
        args += object_of(src);
    }
    args += {"/Fe" + @{output}};
    return {command(@{toolchain.c}, args)};
}

target main() {
    list<target_ref> depends_on = {app};
}

target app() {
    list<string> outputs = {@{output}};
    list<action> actions = compile_actions() + link_actions();
}
```

### 示例 2：多平台目标

**project.ccm**

```
"platform": "linux";
"toolchain": {
    "c": "cc",
    "linker": "cc"
};
"cflags": {"-O2", "-Wall"};
```

**build.cmk**

```c
@include "project.ccm";

func cflags() -> list<string> {
    list<string> flags = @{cflags};
    if (@{platform} == "windows") {
        flags += "/W4";
    } else {
        flags += "-Wextra";
    }
    return flags;
}

target main() {
    list<target_ref> depends_on = {app};
}

target app() {
    list<string> outputs = {"build/app"};
    list<action> actions = {
        mkdir("build"),
        command(@{toolchain.c}, cflags() + {"-c", "src/main.c", "-o", "build/main.o"}),
        command(@{toolchain.linker}, {"build/main.o", "-o", "build/app"})
    };
}
```

条件只影响动作和参数，不改变目标身份。

### 示例 3：聚合目标

```c
target main() {
    list<target_ref> depends_on = {app, assets};
}

target app() {
    list<target_ref> depends_on = {core, main_obj};
    list<string> outputs = {"build/app"};
    list<action> actions = {
        mkdir("build"),
        command(@{toolchain.linker}, {"build/main.o", "build/core.o", "-o", "build/app"})
    };
}

target main_obj() {
    list<string> outputs = {"build/main.o"};
    list<action> actions = {
        mkdir("build"),
        command(@{toolchain.c}, {@{cflags}, "-c", "src/main.c", "-o", "build/main.o"})
    };
}

target core() {
    list<string> outputs = {"build/core.o"};
    list<action> actions = {
        mkdir("build"),
        command(@{toolchain.c}, {@{cflags}, "-c", "src/core.c", "-o", "build/core.o"})
    };
}

target assets() {
    list<string> outputs = {"build/assets.stamp"};
    list<action> actions = {
        mkdir("build"),
        copy("assets/logo.png", "build/logo.png")
    };
}
```

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
| 外部命令 | 不可用 | `command` 目标动作（Build 阶段执行） |

### 关键字速查

| 关键字 | 用途 |
|--------|------|
| `string` | 字符串类型 |
| `int` | 整数类型 |
| `bool` | 布尔类型 |
| `float` | 浮点数类型 |
| `list<T>` | 同质列表类型 |
| `action` | 目标动作类型 |
| `target` | 构建目标声明 |
| `target_ref` | 静态目标引用类型 |
| `const` | 常量修饰 |
| `if` / `else` | 条件分支 |
| `for` / `in` | 遍历循环 |
| `while` | 条件循环 |
| `break` / `continue` | 循环控制 |
| `func` / `return` | 函数定义与返回 |
| `void` | 无返回值函数的返回类型 |
| `@include` | 导入 CCM 数据 |
| `@import` | 导入 CMK 函数或目标 |
| `from` / `as` | 导入修饰子句 |
| `true` / `false` | 布尔字面量 |
| `_` | 显式丢弃返回值 |

### 常见问题

**Q：为什么所有有返回值的函数返回值都必须处理？**

防止遗忘错误处理。构建流程中忽略返回值是常见的 bug 来源——无论是忽略命令退出码还是丢弃函数的计算结果。如果真的不关心结果，显式写 `_ = func(...)` 表明这是有意为之。

**Q：增量构建怎么做？**

由 Build 引擎的增量模块负责。脚本声明目标的 `inputs`、`outputs` 和 `actions`，引擎根据输入、输出、命令、配置和依赖结果计算签名；脚本不自行判断是否需要重新编译。

**Q：怎样复用其他 `.cmk` 中的函数？**

```c
@import func compile from "rules/compile.cmk";
```

只导入你需要的函数，不会引入被导入文件的其他副作用。