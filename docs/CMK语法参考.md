# CMK 语法参考

`.cmk` 是构建脚本，分两段执行：

```text
Configure：执行脚本、求值 target、生成 Plan
Build：Scheduler 调度，Executor 执行目标动作
```

Configure 阶段不创建进程；外部命令只能作为目标动作，在 Build 阶段执行。

## 1. 文件结构

```text
module      := top_item*
top_item    := include | import | var_decl | func_decl | target_decl | top_if
```

- `@include` 引入 `.ccm` 配置；
- `@import` 引入其他 `.cmk` 的函数或 target；
- 变量、函数、target 可以出现在顶层；
- 顶层 `if` 只能选择 target；
- 语句以 `;` 结束，代码块用 `{}`。

## 2. 词法

### 2.1 注释

```c
// 行注释
/* 块注释，可跨行 */
```

### 2.2 标识符与关键字

标识符由字母、数字、下划线组成，不能以数字开头，大小写敏感。

关键字：

```text
string int float bool void list action target target_ref
const if else for in while break continue
func return @include @import from as true false
_
```

保留字不能作为变量名、函数名或 target 名。

### 2.3 数字

```text
42
0
3.14
.5
1.
1e3
2.5E-2
```

- 十进制；
- 负号是独立一元运算符，`-9223372036854775808` 按 `INT64_MIN` 处理；
- `123abc`、`1.2.3` 是词法错误；
- 超出 `INT64` 范围报错。

### 2.4 字符串

```c
"hello"
""
"line1\nline2"
```

支持的转义：

| 转义 | 结果 |
|---|---|
| `\\` | 反斜杠 |
| `\"` | 双引号 |
| `\'` | 单引号 |
| `\n` | 换行 |
| `\r` | 回车 |
| `\t` | 制表 |
| `\a` `\b` `\f` `\v` | 对应控制字符 |
| `\0` | NUL 字节 |

- 字符串不能跨行；
- 其他转义序列报错；
- 字符串可以按字节包含 NUL，长度由内部长度字段维护。

### 2.5 配置引用 `@{...}`

```c
@{toolchain.cc}
@{cflags}
@{defines[0]}
@{toolchain.include[1]}
```

- 花括号内只允许标识符字符、`.`、`[`、`]`；
- 不允许空白和换行；
- `@{}` 与未闭合引用报错；
- 读取 `.ccm` 中已解析的字段；
- 读取结果保留配置值类型：数字仍是数字，数组仍是 `list<T>`。

## 3. 类型

| 类型 | 示例 | 说明 |
|---|---|---|
| `void` | `void` | 仅函数返回类型 |
| `bool` | `true` / `false` | 布尔 |
| `int` | `42` | 有符号 64 位 |
| `float` | `3.14` | `double` |
| `string` | `"text"` | 字符串 |
| `list<T>` | `list<string>` | 同质列表 |
| `action` | `command(...)` | 目标动作 |
| `target_ref` | `core` / `alias.core` | 仅用于 `depends_on` |

`list<T>` 不支持嵌套列表。空列表 `{}` 的元素类型由声明、函数返回类型或 target 字段上下文确定。

## 4. 变量与赋值

```c
string cc = "gcc";
int jobs = 8;
bool verbose = false;
float scale = 2.5;
const string project = "demo";
list<string> sources = {"a.c", "b.c"};
```

- 变量可以声明为 `const`，之后不能重新赋值；
- 模块级变量在模块内可见；
- 局部变量只在当前块或函数内可见；
- 未声明、重复声明、类型不匹配都会在 Semantic 阶段报错。

赋值：

```c
jobs = jobs + 1;
sources += "c.c";
_ = f();
```

- `=` 赋值；
- `+=` 对数值累加，对列表追加一个元素；
- 有返回值的调用必须赋给变量或用 `_` 丢弃。

## 5. 运算符

优先级从高到低：

| 级别 | 运算符 | 结合性 |
|---|---|---|
| 1 | 调用 `()`、索引 `[]`、成员 `.` | 左 |
| 2 | 一元 `!` `-` `+` | 右 |
| 3 | `*` `/` `%` | 左 |
| 4 | `+` `-` | 左 |
| 5 | `<` `>` `<=` `>=` | 非链式 |
| 6 | `==` `!=` | 左 |
| 7 | `&&` | 左 |
| 8 | `\|\|` | 左 |

规则：

- `+` 支持数值加法、字符串拼接、同类型列表连接；
- `-` `*` `/` `%` 用于数值；
- 比较结果是 `bool`；
- `&&` / `||` 短路求值；
- 不支持链式比较，`a < b < c` 报错；
- 没有隐式类型转换；
- 赋值只能作为语句，不能写在表达式里。

## 6. 列表

```c
list<string> files = {"a.c", "b.c"};
files += "c.c";
string first = files[0];
int n = len(files);
string all = join(files, ",");
```

- 索引从 0 开始；
- 越界报运行时错误；
- `list<T>` 只能保存 `T` 类型元素；
- `list<T> + list<T>` 连接；
- `list<T> += T` 追加；
- 列表字面量 `{}` 不能嵌套列表。

## 7. 控制流

### if / else

```c
if (condition) {
    ...
} else if (other) {
    ...
} else {
    ...
}
```

条件必须是 `bool`。

### while

```c
while (condition) {
    ...
}
```

### for-in

```c
for (string file in files) {
    info("%s", file);
}
```

- 遍历 `list<T>`；
- 循环变量类型必须与元素类型匹配；
- 支持 `break` / `continue`。

### return

```c
func f() -> int {
    return 1;
}
```

- `return;` 只用于 `-> void`；
- 非 void 函数所有路径必须返回值；
- `return` 表达式中的函数调用由 VM 处理，不受固定 C 栈深度限制。

### 顶层 if

顶层 `if` 只能包含 target 声明：

```c
if (@{platform} == "windows") {
    target main() { ... }
} else {
    target main() { ... }
}
```

## 8. 函数

```c
func name(type param, ...) -> return_type {
    ...
}
```

示例：

```c
func object_of(string src) -> string {
    return subst(src, ".c", ".o");
}

func cflags() -> list<string> {
    return {"-O2", "-DNDEBUG"};
}

func noop() -> void {
    return;
}
```

- 返回类型必须显式声明；
- 参数按值复制；
- 函数可递归；
- 函数可调用同模块函数或已导入函数；
- 函数内可访问模块级变量和 `@{...}`；
- 不支持闭包、函数指针、匿名函数、动态调用；
- `error()` 是不返回内建，调用后终止 Configure。

## 9. 模块导入

### 引入 CCM

```c
@include "config/project.ccm";
```

### 导入函数

```c
@import func compile from "rules/compile.cmk";
@import func compile, link from "rules/common.cmk";
@import func * from "utils/helpers.cmk";
@import func compile as gcc_compile from "rules/gcc.cmk";
```

### 导入 target

```c
@import target core, utils from "targets/common.cmk" as common;
```

- 函数导入与 target 导入相互独立；
- target 通过 `common.core`、`common.utils` 引用；
- 未导入的跨模块 target 不能直接引用；
- 模块级变量不跨模块导出；
- 循环导入报错。

## 10. target

```c
list<string> sources = find_files("*.c", {"src"});

target app() {
    list<target_ref> depends_on = {core};
    list<string> inputs = sources;
    list<string> outputs = {@{output}};
    list<action> actions = {
        mkdir(dirname(@{output})),
        command("cc", @{cflags} + sources + {"-o", @{output}})
    };
}
```

字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `depends_on` | `list<target_ref>` | 依赖目标，必须成功后才执行 |
| `inputs` | `list<string>` | 缓存签名输入；不会自动加入命令 argv |
| `outputs` | `list<string>` | 缓存签名输出；缺失或变化使缓存失效 |
| `actions` | `list<action>` | 按顺序执行的动作 |

规则：

- target 只能出现在顶层，或顶层 `if` 分支；
- 目标身份是 `模块路径 + target 名`；
- 本模块 target 直接写名字；
- 跨模块 target 写 `别名.目标名`；
- 聚合目标可以只有 `depends_on`；
- 空 target 至少需要依赖、输入、输出或动作之一；
- 默认构建目标为 `main`，可用 `-t name` 指定；
- `inputs` / `outputs` 是构建元数据，Executor 不会自动拼接或创建它们；
- target 字段名不是 `actions` 作用域的变量；在 `actions` 中直接引用 `inputs` 会报“未定义的标识符”；
- 需要让同一批文件同时进入 `inputs` 和命令 argv 时，把列表存成模块级变量：

```c
list<string> sources = find_files("*.c", {"src"});

target app() {
    list<string> inputs = sources;
    list<string> outputs = {@{output}};
    list<action> actions = {
        mkdir(dirname(@{output})),
        command("cc", @{cflags} + sources + {"-o", @{output}})
    };
}
```

- `actions` 顺序执行，失败即停止当前目标。

## 11. 动作

### command

```c
command(program, args)
command(program, args, cwd)
command(program, args, cwd, env)
```

```c
command("gcc", {"-O2", "src/main.c", "-o", "build/app"})
command("cmake", {"--build", "."}, "build")
command("cc", {"main.c"}, "src", {"CC=gcc", "CFLAGS=-O2"})
```

- `program`：可执行文件路径或 PATH 中的名字；
- `args`：`list<string>`，直接传给进程，不经过 shell；
- `cwd`：可选工作目录；
- `env`：可选 `list<string>`，元素形如 `"K=V"`；
- 返回 `action`；
- 进程输出由 Executor 捕获，Scheduler 按稳定顺序输出。

### mkdir

```c
mkdir("build")
```

创建目录，返回 `action`。

### copy

```c
copy("from", "to")
```

复制文件。

### move

```c
move("from", "to")
```

移动文件。

### remove

```c
remove("path")
```

删除文件或空目录。

文件动作路径按当前工作目录解析；动作只做单一文件操作，不做通配符展开。

## 12. 内建函数

所有内建函数如下。除 `error` 外均可作为表达式调用；有返回值的调用必须赋值或用 `_` 丢弃。

### len

```c
len(list<T>) -> int
```

返回列表元素数量。

### join

```c
join(list<string>, string) -> string
```

用分隔符连接字符串列表。

```c
join({"a", "b"}, ",")  // "a,b"
```

### subst

```c
subst(string, string, string) -> string
```

把第一个字符串中所有 `from` 替换为 `to`。

```c
subst("main.c", ".c", ".o")  // "main.o"
```

### basename

```c
basename(string) -> string
```

取路径最后一段。

### dirname

```c
dirname(string) -> string
```

取路径父目录。

### host_info

```c
host_info(string) -> string
```

支持键：

| 键 | 返回 |
|---|---|
| `os` | `windows` / `linux` / `android` 等 |
| `arch` | `amd64` / `arm64` / `x86` / `arm` 等 |
| `cpu_count` | CPU 数量字符串 |
| `path_separator` | 平台路径分隔符 |

示例：

```c
if (host_info("os") == "windows") {
    ...
}
```

### find_files

```c
find_files(string, list<string>) -> list<string>
```

在多个根目录下按通配符查找文件，返回排序、去重的路径列表。

```c
list<string> sources = find_files("*.c", {"src", "generated"});
```

- 第一个参数是文件名模式，支持 `*` / `?`；
- 第二个参数是根目录列表；
- 相对根目录按当前工作目录解析；
- 只返回文件；
- 返回结果排序并去重；
- 受 `CCMAKE_CMK_MAX_FIND_DEPTH` 约束。

### command / mkdir / copy / move / remove

见第 11 节。

### info

```c
info(string, ...) -> void
```

输出信息。格式串支持 `%s`、`%d`、`%f`、`%%`。

```c
info("building %s", name);
```

### warn

```c
warn(string, ...) -> void
```

输出警告，不终止 Configure。

### error

```c
error(string, ...) -> void
```

输出错误并终止 Configure；属于不返回函数。

```c
error("unsupported platform: %s", @{platform});
```

## 13. 执行策略

以下环境变量在 Program 初始化时读取；值为 0 表示不限制：

| 环境变量 | 作用 |
|---|---|
| `CCMAKE_CMK_MAX_STEPS` | Configure 最大执行步数 |
| `CCMAKE_CMK_MAX_RECURSION_DEPTH` | 函数调用深度策略 |
| `CCMAKE_CMK_MAX_CONFIGURE_TIME_MS` | Configure 最大 CPU 时间 |
| `CCMAKE_CMK_MAX_FIND_DEPTH` | `find_files` 最大目录深度 |
| `CCMAKE_CMK_MAX_PROCESSES` | Scheduler READY 并发上限 |
| `CCMAKE_CMK_MAX_OUTPUT_CAPTURE` | 单进程输出捕获上限 |

命令行 `-j` 覆盖构建并发数；`-B` 强制绕过缓存；`-k` 继续独立目标。

## 14. 完整示例

```c
@include "project.ccm";

// 源码由检索得到，不在脚本中枚举文件名。
list<string> sources = find_files("*.c", {"src"});

func build_actions() -> list<action> {
    return {
        mkdir(dirname(@{output})),
        command(
            @{toolchain.cc},
            @{toolchain.cflags} + sources + {"-o", @{output}}
        )
    };
}

target main() {
    list<target_ref> depends_on = {app};
}

target app() {
    list<string> inputs = sources;
    list<string> outputs = {@{output}};
    list<action> actions = build_actions();
}
```

构建：

```bat
ccmake -c project.ccm -f build.cmk
ccmake -c project.ccm -f build.cmk -t app
```

## 15. 常见错误

| 错误 | 原因 |
|---|---|
| 缺少分号 | 语句未以 `;` 结束 |
| 未处理的返回值 | 有返回值调用没有赋值给变量或 `_` |
| 类型不匹配 | 变量、参数、列表元素类型不一致 |
| 未知配置字段 | `@{...}` 路径在 `.ccm` 中不存在 |
| 目标不存在 | `depends_on` 引用了未导入或未声明的 target |
| target 位置非法 | target 出现在函数、循环或动作列表内 |
| 非 void 函数缺少返回值 | 存在未 return 的控制流路径 |
| 列表嵌套 | `list<list<T>>` 不支持 |
| 赋值当表达式 | `if (x = 1)` 或 `f(x = 1)` |
| 链式比较 | `a < b < c` |
| 调用非裸标识符 | `(f)()`、`f()()` 不支持 |
