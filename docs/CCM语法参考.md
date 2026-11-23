# CCM 语法参考

`.ccm` 是声明式配置格式，只描述数据，不包含控制流、函数或外部动作。`.cmk` 通过 `@{路径}` 读取这些数据。

## 1. 文件结构

```text
file        := statement*
statement   := include | field
include     := "@include" string ";"
field       := string ":" value ";"
value       := string | number | "true" | "false"
             | object | array | builtin
object      := "{" (string ":" value ";")* "}"
array       := "[" (value (","? value)*)? ","? "]"
builtin     := "${" ("ref" | "find") ... "}"
```

一个 `.ccm` 可以只包含字段，也可以先包含其他 `.ccm`：

```text
@include "common/base.ccm";

"project": "demo";
"cc": "gcc";
"cflags": ["-O2", "-Wall"];
"toolchain": {
    "c": "gcc";
    "linker": "ld";
};
```

## 2. 词法规则

### 2.1 空白与注释

- 空白用于分隔 token。
- `//` 到行尾为行注释。
- `/* ... */` 为块注释，可跨行，不嵌套。
- 注释等价于空白，不产生值。

```text
"a": 1;   // 行注释
/* 块注释 */
"b": 2;
```

### 2.2 键名

键名必须是双引号字符串，内容满足：

```text
[A-Za-z_][A-Za-z0-9_]*
```

- ASCII，大小写敏感；
- 不能以数字开头；
- `.` 表示路径层级，`[n]` 表示数组索引，不属于键名；
- 顶级字段和嵌套对象成员使用同一规则。

### 2.3 字符串

```text
"hello"
""
"path/to/file"
```

- 字符串以双引号开始和结束；
- 不处理反斜杠转义；
- 字符串不能跨行；
- 字符串内不能再出现未转义的双引号。

### 2.4 数字

```text
0
42
-7
3.14
-0.5
1e3
2.5E-2
```

- 只接受十进制；
- 整数解析为 `CCINT64`，超出范围报错；
- 浮点解析为 `double`；
- 负号是数字扫描的一部分；
- 不支持十六进制、八进制、二进制、`nan`、`inf`。

### 2.5 布尔值

```text
true
false
```

## 3. 值类型

| 类型 | 写法 | `.cmk` 中的读取结果 |
|---|---|---|
| string | `"text"` | `string` |
| int | `42` | `int` |
| float | `3.14` | `float` |
| bool | `true` / `false` | `bool` |
| object | `{ ... }` | 对象；通过路径访问成员 |
| array | `[ ... ]` | `list<T>` |
| builtin | `${ref ...}` / `${find ...}` | string |

`.cmk` 读取对象本身不能直接转成字符串；应读取具体成员。

## 4. 对象

对象成员以 `"键": 值;` 书写，成员之间用分号分隔：

```text
"toolchain": {
    "c": "gcc";
    "cxx": "g++";
    "ar": "ar";
};
```

- 对象可以嵌套；
- 成员顺序保留；
- 空对象写成 `{}`；
- 同一对象内重复键按顺序覆盖；
- 对象值参与 `.cmk` 的路径寻址，例如 `@{toolchain.c}`。

### 合并规则

顶层字段对象使用浅合并：

```text
// base.ccm
"toolchain": {
    "c": "gcc";
    "linker": "ld";
};

// project.ccm
@include "base.ccm";
"toolchain": {
    "c": "clang";
};
```

结果：

```text
toolchain.c      = "clang"
toolchain.linker = "ld"
```

对象内部的成员对象使用整体替换，不递归合并；数组元素整体替换。

## 5. 数组

```text
"defines": ["A", "B"];
"files": ["a.c", "b.c",];
```

- 元素用逗号分隔；
- 元素之间也可以用空白分隔；
- 允许尾逗号；
- 支持嵌套数组和数组中的对象；
- 空数组写成 `[]`；
- 数组元素顺序保留；
- `.ccm` 数组在 `.cmk` 中映射为同质 `list<T>`；
- 混合类型数组在 `.cmk` 读取时按元素上下文报错，不能作为同质列表使用。

数组中的对象：

```text
"items": [
    { "name": "a"; "size": 1; },
    { "name": "b"; "size": 2; }
];
```

## 6. `@include`

```text
@include "path/to/base.ccm";
```

- 只接受双引号字面量路径；
- 路径可以相对当前文件或绝对路径；
- 扩展名任意，`.ccm` 是约定；
- 只能出现在文件顶层；
- 语句以分号结束；
- 被包含文件先于当前文件解析；
- 多个 include 按出现顺序解析；
- 同一路径只解析一次；
- 循环 include 报错并给出循环链。

覆盖优先级：

```text
更早的 include < 更晚的 include < 当前文件
```

## 7. 动态值 `${...}`

`${...}` 是 `.ccm` 值位置的内建表达式，不是字符串插值。顶层字符串 `"${ref x}"` 是普通字符串；只有未加引号的 `${...}` 会被求值。

### 7.1 `${ref 路径}`

```text
"source_dir": "./src";
"copy": ${ref source_dir};
"nested": ${ref toolchain.c};
"first_flag": ${ref defines[0]};
```

求值结果按字符串写入当前字段：

| 引用值类型 | 序列化 |
|---|---|
| string | 原字符串 |
| int | 十进制 |
| float | 十进制浮点 |
| bool | `true` / `false` |
| array | 元素逐个序列化，以单个空格连接 |
| object | 错误：对象不能直接字符串化 |

`${ref}` 读取当前已解析的配置树，不重新读取源文件。

### 7.2 `${find ...}`

```text
${find <目录...> -include <模式...> [-exclude <模式...>]}
```

示例：

```text
"src": "./src";
"sources": ${find ./src -include *.c -exclude *_test.c};
"nested": ${find ${ref src} -include *.c};
"quoted": ${find ./src -include "${ref pattern}"};
```

规则：

- 目录、`-include`、`-exclude` 参数用空白分隔；
- 双引号可保护含空格的参数；
- `-include` 后的模式是包含白名单；
- `-exclude` 后的模式是排除名单；
- 通配符支持 `*` 和 `?`；
- 支持嵌套 `${ref}` / `${find}`；
- 结果只包含文件，不包含目录；
- 结果去重、排序，并以单个空格连接成字符串；
- 相对目录按当前工作目录解析；
- 找不到匹配时返回空字符串。

## 8. `-D` 覆盖

命令行 `-D/--define` 在所有 `.ccm` 解析完成后写入配置树：

```bat
ccmake -c project.ccm -D toolchain.c=clang -D "cflags=-O2 -g"
```

- 键按路径段解释；
- 每段遵循键名规则；
- 值按字符串写入；
- 优先级高于所有 `.ccm` 文件。

## 9. 完整示例

`platform.ccm`：

```text
"toolchain": {
    "cc": "gcc";
    "cflags": ["-O2", "-Wall"];
};
"src": "./src";
```

`project.ccm`：

```text
@include "platform.ccm";

"project": "hello";
"src": "./src";
"output": "build/hello";
```

`build.cmk`：

```c
@include "project.ccm";

list<string> source_files = find_files("*.c", {@{src}});

target main() {
    list<string> inputs = source_files;
    list<string> outputs = {@{output}};
    list<action> actions = {
        mkdir(dirname(@{output})),
        command(
            @{toolchain.cc},
            @{toolchain.cflags} + source_files + {"-o", @{output}}
        )
    };
}
```

## 10. 限制与错误

- 键名非法、缺少冒号、缺少分号、括号不匹配都会在对应行报错；
- 对象成员必须是字符串键；
- `${ref}` 路径不存在时报错；
- `${ref}` 引用对象时报错；
- `${find}` 目录不存在时跳过并继续；
- include 循环、重复路径校验失败时报错；
- 分配失败返回明确错误，不发布半成品配置。

## 11. 解析顺序

1. 读取根 `.ccm`；
2. 扫描 `@include`，定位并加载闭包；
3. 按 include 后序解析所有文件；
4. 应用字段覆盖；
5. 执行 `${ref}` / `${find}`；
6. 应用 `-D` 覆盖；
7. 把结果写入全局配置树，供 `.cmk` 的 `@{...}` 读取。
