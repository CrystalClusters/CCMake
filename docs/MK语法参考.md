# MK 语法参考

`.mk` 是脚本。CCMake 固定执行入口：

```c
func main() -> void
```

缺少 `main` 时报告“缺少入口 main”；动作按源码顺序立即执行。

## 1. 文件结构

```text
module      := top_item*
top_item    := include | import | var_decl | func_decl
```

- `@include` 引入 `.mc` 配置；
- `@import` 引入其他 `.mk` 的函数；
- 变量、函数可以出现在顶层；
- 顶层只允许 include、import、变量与函数声明；
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
string int float bool void list
const if else for in while break continue
func return @include @import from as true false
_
```

保留字不能作为变量名或函数名。

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
- 读取 `.mc` 中已解析的字段；
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

`list<T>` 不支持嵌套列表。空列表 `{}` 的元素类型由声明或函数返回类型确定。

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

## 8. 函数

```c
func name(type param, ...) -> return_type {
    ...
}
```

示例：

```c
func object_of(string src) -> string {
    return patsubst("%.c", "%.o", src);
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

## 9. 模块导入

### 引入 MC

```c
@include "config/project.mc";
```

### 导入函数

```c
@import func compile from "rules/compile.mk";
@import func compile, link from "rules/common.mk";
@import func * from "utils/helpers.mk";
@import func compile as gcc_compile from "rules/gcc.mk";
```

- 模块级变量不跨模块导出；
- 循环导入报错。

## 10. 入口 main

入口固定为：

```c
func main() -> void {
    ...
}
```

- `main` 必须无参数、返回 `void`；
- 不能通过命令行指定其他入口；
- `main` 内可以调用任意普通函数；
- `main` 内的动作按源码顺序立即执行。

```c
func main() -> void {
    info("cc: %s", @{toolchain.cc});
    mkdir("build");
    command("cc", {"main.c", "-o", "build/app"});
}
```

## 11. 动作

`mkdir`、`copy`、`move`、`remove`、`command` 都是 void 函数，调用即执行，失败即终止脚本。

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
- 调用即执行；进程输出写入 stdout。

### mkdir

```c
mkdir("build")
```

创建目录。

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
subst(from, to, text) -> string
```

把 `text` 中所有 `from` 替换为 `to`，保留原有空白；`from` 为空时原样返回 `text`。

```c
subst(".c", ".o", "main.c")  // "main.o"
```

### patsubst

```c
patsubst(pattern, replacement, text) -> string
```

按 make 的 `patsubst` 规则处理：按空白拆分单词，输出单词之间使用单个空格；`pattern` 和 `replacement` 中第一个 `%` 是通配符，未匹配的单词原样保留。

```c
patsubst("%.c", "%.o", "a.c b.c c.h")       // "a.o b.o c.h"
patsubst("src/%.c", "build/%.o", "src/a.c")  // "build/a.o"
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
- 受 `CCMAKE_MK_MAX_FIND_DEPTH` 约束。

### file_exists

```c
file_exists(string) -> bool
```

判断路径是否为已存在的普通文件。

```c
if (file_exists(@{output})) {
    info("output exists");
}
```

- 只有普通文件返回 `true`；
- 目录、缺失路径返回 `false`；
- 相对路径按当前工作目录解析。

### file_mtime

```c
file_mtime(string) -> int
```

返回文件修改时间，Unix 秒。

```c
int t = file_mtime("build/app");
```

- 文件不存在返回 `-1`；
- 目录也可返回时间，但通常应配合 `file_exists` 使用；
- 精度取决于宿主文件系统，通常是秒级。

### file_newer

```c
file_newer(string, string) -> bool
```

判断第一个文件是否比第二个文件更新。

```c
if (file_newer("src/main.c", "build/main.o")) {
    ...
}
```

- 两个路径都必须是存在的文件；
- 任一文件缺失返回 `false`；
- 使用宿主文件系统的亚秒级修改时间比较；
- 可用于手动判断对象文件/可执行文件是否过期。

## 手动增量控制

是否执行动作由脚本条件决定：

```c
func main() -> void {
    list<string> sources = find_files("*.c", {"src"});

    if (!file_exists(@{output}) ||
        file_newer("src/main.c", @{output})) {
        mkdir(dirname(@{output}));
        command(
            @{toolchain.cc},
            @{toolchain.cflags} + sources + {"-o", @{output}}
        );
    }
}
```

多文件逐对象判断：

```c
for (string src in sources) {
    string obj = "build/" + patsubst("%.c", "%.o", basename(src));
    if (!file_exists(obj) || file_newer(src, obj)) {
        command(
            @{toolchain.cc},
            @{toolchain.cflags} + {"-c", src, "-o", obj}
        );
    }
}
```

CCMake 不决定哪些动作需要执行；源码顺序就是执行顺序。

## 13. 执行策略

以下环境变量在 Program 初始化时读取；值为 0 表示不限制：

| 环境变量 | 作用 |
|---|---|
| `CCMAKE_MK_MAX_STEPS` | Configure 最大执行步数 |
| `CCMAKE_MK_MAX_RECURSION_DEPTH` | 函数调用深度策略 |
| `CCMAKE_MK_MAX_CONFIGURE_TIME_MS` | Configure 最大 CPU 时间 |
| `CCMAKE_MK_MAX_FIND_DEPTH` | `find_files` 最大目录深度 |
| `CCMAKE_MK_MAX_OUTPUT_CAPTURE` | 单进程输出捕获上限 |

执行顺序完全由源码顺序决定；没有并发调度。

## 14. 完整示例

```c
@include "project.mc";

func main() -> void {
    list<string> sources = find_files("*.c", {"src"});
    info("cc: %s", @{toolchain.cc});

    if (!file_exists(@{output}) ||
        file_newer("src/main.c", @{output})) {
        mkdir(dirname(@{output}));
        command(
            @{toolchain.cc},
            @{toolchain.cflags} + sources + {"-o", @{output}}
        );
    }
}
```

构建：

```bat
ccmake -c project.mc -f build.mk
```

没有必填字段；`main` 为空或不存在时正常退出。

## 15. 常见错误

| 错误 | 原因 |
|---|---|
| 缺少分号 | 语句未以 `;` 结束 |
| 未处理的返回值 | 有返回值调用没有赋值给变量或 `_` |
| 类型不匹配 | 变量、参数、列表元素类型不一致 |
| 未知配置字段 | `@{...}` 路径在 `.mc` 中不存在 |
| 非 void 函数缺少返回值 | 存在未 return 的控制流路径 |
| 列表嵌套 | `list<list<T>>` 不支持 |
| 赋值当表达式 | `if (x = 1)` 或 `f(x = 1)` |
| 链式比较 | `a < b < c` |
| 调用非裸标识符 | `(f)()`、`f()()` 不支持 |
