# CMK 实现细则

本文记录 `.cmk` 的目标架构、语言边界、模块划分与实施顺序，是实现依据；[CMK 文件规范](CMK文件规范.md) 继续作为语言语法与内置函数的参考。

## 1. 定位与硬约束

`.cmk` 不是只面向单一项目的构建脚本，而是 CCMake 的通用流程描述语言。设计必须同时满足：

1. **语言核心保持图灵完备**：变量、赋值、函数、递归、条件、循环和导入缺一不可；外部命令由 Build 阶段的目标动作提供。
2. **构建语义独立于语法**：Parser 只生成 AST；语义分析验证 AST；Lowering 生成构建 IR；Scheduler 只消费已经冻结的 IR。
3. **C 风格语法**：声明、赋值、函数、条件、循环、块作用域和运算符均以 C 为参考；没有明确方案时采用 C 的处理方式。
4. **Configure 阶段不执行外部命令**：平台、工具链、架构等信息可由 `.ccm` 配置变量注入，也可通过只读 `host_info` / `find_files` 内建获取；内建不得创建进程，外部命令只作为 Build 阶段的目标动作执行。
5. **目标模型是一等能力**：`target` 声明构建节点，默认目标名为 `main`，目标不是普通函数，但按函数式入口选择。
6. **资源所有权唯一**：每个资源只有一个所有者；转移显式；借用者不释放、不延长生命周期。
7. **无隐式行为**：不自动拼接 shell 字符串，不隐式读取文件，不在 Parser 阶段修改构建图。

## 2. 语言核心

### 2.1 语言能力

| 能力 | 决策 |
|---|---|
| 类型 | `int`、`float`、`bool`、`string`、`list<T>`、`action`、`target_ref` |
| 变量 | C 风格类型声明，块作用域，可重新赋值 |
| 函数 | 支持参数、返回值、递归 |
| 条件 | `if` / `else if` / `else` |
| 循环 | `while`、`for-in`、`break`、`continue` |
| 导入 | 支持 `@import` 与别名 |
| 配置读取 | 支持 `@{path}` |
| 内建函数 | 注册表提供，与用户函数同调用语法 |
| 外部命令 | 仅通过目标动作 `command` 在 Build 阶段执行 |
| 入口 | 默认选择目标 `main()`，`-t` 选择其他目标 |
| 目标 | `target` 声明，生成构建 IR |
| 异常 | 无异常；错误显式返回或终止当前阶段 |
| 并发 | 由 Scheduler 控制，脚本不能自行创建线程 |

不引入闭包、动态代码执行、反射、宏系统、泛型和用户自定义类型。

### 2.2 变量与作用域

采用 C 风格：

```c
string cc = @{toolchain.c};
int retries = 3;
list<string> files = @{sources};

if (cc == "gcc") {
    string profile = "gnu";
}
```

规则：

- 每个变量必须显式声明类型；
- 块作用域，离开块即销毁；
- 支持重新赋值，赋值类型必须匹配；
- 函数参数与局部变量属于函数作用域；
- 函数可以读取配置 `@{...}`；
- 不引入隐式全局变量；
- `const` 变量的值不可重新赋值；
- 变量在声明处初始化。

#### 列表类型

`list<T>` 是同质列表，`T` 是元素类型：

```c
list<string> args = {"-O2", "-c", "main.c"};
list<action> actions = {mkdir("build")};
```

- 不允许裸 `list`，元素类型必须写清楚；
- 不允许嵌套列表；
- 空列表 `{}` 的元素类型由声明、函数返回类型或目标字段上下文确定；
- `+` 只连接两个元素类型相同的列表；
- `+=` 只追加一个元素，元素类型必须与列表元素类型一致；
- 索引 `list[i]` 从 0 开始，越界在运行时报告错误；
- `.ccm` 数组映射为同质 `list<T>`；空数组需要上下文类型，混合或嵌套数组报错；
- `action` 只能作为值传递和存入 `list<action>`，不能直接执行；
- `target_ref` 只用于 `depends_on`，不是普通用户变量类型。

### 2.3 函数

```c
func compiler_flags(string profile) -> list<string> {
    if (profile == "release") {
        return {"-O2", "-DNDEBUG"};
    }
    return {"-O0", "-g"};
}
```

- 函数是 Turing 完备性的核心，不允许删除；
- 支持递归；
- 支持 `return`；
- 返回类型必须显式声明；无返回值写 `-> void`，省略返回类型是语义错误；
- `return;` 只能用于 `-> void`；非 void 函数的所有控制流路径都必须返回值；
- 返回值必须被使用，或显式赋给 `_`；
- 函数可以返回 `action` 或 `list<action>`，用于构造目标动作；
- 函数内部不能执行外部命令，也不能声明 `target`；
- 函数只做配置计算：读取 `@{...}`、运算、条件、循环、返回值和列表构造。

`error(...)` 的类型是 `-> void`，但标记为不返回：调用后当前 Configure 控制流终止，不生成可执行 Plan。非 void 函数在 `error()` 之后的路径不需要返回值；其他路径必须返回值。

### 2.4 外部命令

Configure 阶段不执行外部命令，也不创建进程。平台、工具链、架构、编译器等可以由 `.ccm` 配置变量传入，也可以由只读 `host_info` 查询系统信息；文件搜索使用 `find_files`。宿主访问仅限注册表中的只读内建：

```c
string target_platform = @{platform};
string host_os = host_info("os");
string cc = @{toolchain.c};

if (target_platform == "windows") {
    list<string> flags = {"-D_WIN32", "-O2"};
}
```

`target_platform` 是构建目标平台，`host_os` 是当前运行 CCMake 的主机系统；交叉编译时两者可以不同。

外部命令只作为目标动作，在 Build 阶段执行：

```c
target app() {
    list<action> actions = {
        mkdir("build"),
        command("cc", {"-O2", "-c", "main.c", "-o", "main.o"})
    };
}
```

- `command(program, args [, cwd [, env]])` 创建延迟动作；
- `mkdir(path)` / `copy(from, to)` / `move(from, to)` / `remove(path)` 创建文件动作；
- 动作参数在 Configure 阶段求值并复制进 IR；
- 动作不持有 AST 节点或脚本变量指针；
- 配置变量缺失时按语言规则报错，不自动回退到外部命令；需要系统信息时显式调用 `host_info`；
- 不支持隐式 shell、管道、重定向；
- 需要 shell 语义时显式调用 `sh` / `cmd`，由脚本自行承担风险。

### 2.5 函数调用的可行性

函数调用是语言核心的一部分，不依赖外部命令，也不依赖 Build 阶段。

实现路径：

1. **Parser**：生成 `CMK_AST_CALL`，保存函数名、实参列表和源码位置。
2. **Semantic**：把函数名解析为函数符号；检查实参数量、类型和返回类型；检测未定义函数。
3. **Value Runtime**：建立调用帧，保存参数、局部变量和返回地址；返回值按值复制到调用者。调用帧存放在 Runtime 的动态数组中，不使用固定深度配额。
4. **深度**：CMK 语言层不设递归次数上限；递归只受可用内存和宿主资源约束，分配失败返回明确诊断。
5. **所有权**：参数和返回值使用值复制或 arena 借用；被调用函数不能释放调用者的值；返回的 list/string 由调用方持有或由 arena 统一回收。

支持：

```c
func flags(string profile) -> list<string> {
    if (profile == "release") {
        return {"-O2", "-DNDEBUG"};
    }
    return {"-O0", "-g"};
}

func log_flags(string profile) -> void {
    info("profile: %s", profile);
}

target app() {
    list<action> actions = {
        command("cc", flags(@{mode}) + {"src/main.c", "-o", "build/app"})
    };
}
```

- 支持递归，不设 CMK 层递归次数上限；
- 不支持闭包、函数指针、匿名函数和动态调用；
- 函数调用可以出现在顶层变量初始化、`if`/`for` 条件、目标字段和动作参数中，全部在 Configure 阶段求值；
- 函数调用不能出现在 `target` 名或 `depends_on` 的目标引用位置，目标身份保持静态；
- 函数不能声明目标，也不能执行外部命令；
- 内置函数与用户函数共用调用语法，但内置函数由语义层绑定，不进入用户符号表。

结论：函数调用实现成本主要在作用域、类型检查和调用帧，不涉及进程或构建图，适合在 M3/M4 完成，是可控的。

可行性分解：

| 部分 | 难度 | 说明 |
|---|---|---|
| Parser | 低 | 调用表达式与普通表达式同层 |
| 符号解析 | 中 | 函数名、参数数量、返回类型、递归符号 |
| 类型检查 | 中 | 实参/返回值、list 元素类型、`_` 丢弃 |
| 调用帧 | 中 | 参数、局部变量、返回地址、动态帧数组 |
| 值所有权 | 中 | 参数复制、返回值移动/复制、arena |
| 错误传播 | 低 | 统一 `CCFALSE` + 诊断 |

主要风险是递归的内存占用和 list/string 返回值复制成本；用动态调用帧和 arena 分配处理，不设置语言层递归配额，不引入闭包或函数指针。

### 2.6 内建函数与扩展接口

内建函数与用户函数使用相同的调用语法、参数检查和返回值规则：

```c
int n = len(@{sources});
list<string> hits = find_files("*.c", {"src", "tests"});
string os = host_info("os");
```

内建函数不是语法特例，而是由注册表提供的原生函数。Parser 只生成普通调用节点，Semantic 根据内建签名检查参数，Runtime 调用对应的原生处理函数。

所有内建调用都在 Configure 阶段求值；Build 阶段只执行已经生成的动作，不重新调用内建。

#### 分类

| 类别 | 示例 | 阶段 | 说明 |
|---|---|---|---|
| 纯计算 | `len`、`join`、`subst`、`basename`、`dirname` | Configure | 无宿主副作用 |
| 只读查询 | `find_files`、`host_info` | Configure | 读文件系统或系统信息，不创建进程 |
| 动作构造 | `command`、`mkdir`、`copy`、`move`、`remove` | Configure | 返回 `action`，Build 阶段执行 |
| 诊断 | `info`、`warn`、`error` | Configure | 输出日志；`error` 带 `CMK_BUILTIN_NORETURN`，终止配置 |

`host_info` 只接受固定键：`os`、`arch`、`cpu_count`、`path_separator`。工具链信息仍由 `.ccm` 注入，不在内建中执行编译器探测。

#### 注册接口

```c
#define CMK_BUILTIN_ABI_VERSION 1u

typedef enum cmk_builtin_flags {
    CMK_BUILTIN_PURE        = 1u << 0,
    CMK_BUILTIN_READ_FS     = 1u << 1,
    CMK_BUILTIN_QUERY_HOST  = 1u << 2,
    CMK_BUILTIN_BUILD_ACTION = 1u << 3,
    CMK_BUILTIN_VARIADIC    = 1u << 4,
    CMK_BUILTIN_NORETURN    = 1u << 5
} CMKBuiltinFlags;

typedef struct cmk_builtin_arg {
    const char *name;
    CMKValueType type;
    CCBOOL optional;
} CMKBuiltinArg;

typedef struct cmk_builtin_call {
    CMKRuntime *runtime;       /* 不透明运行时 */
    CMKValue *args;            /* 借用，处理函数不得释放 */
    CCUINT32 argc;
    CMKValue *out;             /* 由运行时初始化；成功时写入拥有值 */
    CMKDiagnostic *diagnostic; /* 失败时写入位置和说明 */
} CMKBuiltinCall;

typedef CCBOOL (*CMKBuiltinFn)(CMKBuiltinCall *call);

typedef struct cmk_builtin_spec {
    CCUINT32 abi_version;
    const char *name;
    const char *signature;     /* 用于诊断展示 */
    const CMKBuiltinArg *args;
    CCUINT32 min_args;
    CCUINT32 max_args;
    CMKValueType return_type;
    CCUINT32 flags;
    CMKBuiltinFn fn;
} CMKBuiltinSpec;

typedef struct cmk_builtin_module {
    CCUINT32 abi_version;
    const char *name;          /* 模块名，建议作为内建名前缀 */
    CCBOOL (*register_fn)(void);
} CMKBuiltinModule;
```

注册表接口：

```c
CCBOOL cmk_builtin_init(void);
void cmk_builtin_shutdown(void);
CCBOOL cmk_builtin_register(const CMKBuiltinSpec *spec);
const CMKBuiltinSpec *cmk_builtin_find(const char *name, CCUINT32 len);
CCBOOL cmk_builtin_register_module(const CMKBuiltinModule *module);
```

一个最小内建示例：

```c
static CCBOOL builtin_host_info(CMKBuiltinCall *call)
{
    if (call->argc != 1 || call->args[0].type != CMK_VALUE_STRING)
    {
        return cmk_builtin_error(call, "host_info 需要一个字符串参数");
    }
    const char *key = call->args[0].as.string.data;
    const char *value = cmk_host_info_lookup(call->runtime, key);
    if (!value)
        return cmk_builtin_error(call, "未知的系统信息键");
    return cmk_value_set_string(call->runtime, call->out, value);
}

static const CMKBuiltinArg host_info_args[] = {
    {"key", CMK_VALUE_STRING, CCFALSE}
};

static const CMKBuiltinSpec host_info_spec = {
    .abi_version = CMK_BUILTIN_ABI_VERSION,
    .name = "host_info",
    .signature = "host_info(string key) -> string",
    .args = host_info_args,
    .min_args = 1,
    .max_args = 1,
    .return_type = CMK_VALUE_STRING,
    .flags = CMK_BUILTIN_PURE | CMK_BUILTIN_QUERY_HOST,
    .fn = builtin_host_info
};
```

处理函数只依赖公开的 `CMKBuiltinCall`、`CMKValue` 和 Runtime 查询接口，不接触 Parser 或 AST。

规则：

- `cmk_builtin_init` / `cmk_builtin_shutdown` 成对，注册表所有者是 Runtime；
- Runtime 初始化时注册标准内建和外部模块；任一模块注册失败则回滚已注册项并终止初始化；
- 内建名全局唯一，用户函数不能覆盖内建名；
- 同名重复注册失败，注册表保持原状；
- `spec`、参数名、签名字符串必须是静态生命周期；
- 注册表初始化完成后只读，Configure 期间不加锁；
- 处理函数不得保存 `args` 指针，不得释放借用值；
- 成功时 `out` 写入 Runtime 拥有的值；失败时不写有效输出；
- 处理函数通过 `diagnostic` 返回错误位置和说明，不直接打印错误；
- `abi_version` 不匹配的模块拒绝注册。

#### 扩展方式

内部新增内建只需：

1. 在 `cmk_builtin_std.c` 或独立模块中实现 `CMKBuiltinFn`；
2. 定义静态 `CMKBuiltinSpec` 和参数表；
3. 在模块的 `register_fn` 中注册；
4. 为签名、错误和所有权补测试；
5. 更新文档。

外部模块通过公开头文件 `cmk_builtin.h` 和 `cmk_value.h` 接入，不包含 Parser、AST、IR 的内部头文件。第一版只支持链接期模块，不支持运行时 `dlopen`。

外部模块规范：

- 必须提供 `cmk_builtin_module` 和 `register_fn`；
- 内建名使用模块前缀，例如 `image_size`、`git_commit`；
- 不直接访问 `.ccm` 树、AST、IR 或 Scheduler；
- 只读查询通过 Runtime 提供的文件/系统查询接口；
- 不允许内建自行创建进程；外部命令统一走 `command` 动作；
- 输出值由 Runtime arena 分配，模块不保留跨调用指针。

#### 资源与失败

- 内建参数数量由签名决定，不是额外配额；
- 结果字符串和列表由 Runtime arena 分配；内存不足返回明确诊断；
- 不设置内建调用总次数上限；
- `find_files` 使用已访问集合避免文件系统循环，不截断结果数量或遍历深度；
- `host_info` 键白名单属于接口定义，不是能力限制；
- 诊断信息动态分配，不设固定长度上限。

### 2.7 模块与导入

一个 `.cmk` 文件是一个模块。模块身份是规范化后的绝对路径；同一文件通过不同相对路径或符号链接到达时，按同一模块处理。Windows 路径比较不区分大小写，POSIX 区分大小写。

#### 函数导入与目标导入分离

函数导入只引入函数符号，不暴露目标：

```c
@import func compile, link from "rules/compile.cmk";
@import func compile as gcc_compile from "rules/gcc.cmk";
```

目标导入必须显式列出目标，并绑定模块别名：

```c
@import target core, utils from "targets/common.cmk" as common;
```

- 函数导入不把被导入模块的 `target` 加入当前计划；
- 目标导入不把被导入模块的函数并入当前函数命名空间；
- 目标通过 `别名.目标名` 引用，例如 `common.core`；
- 目标别名在导入文件中唯一；同一模块重复使用不同别名报错；
- 函数可以用 `as` 单独重命名；`*` 只用于函数批量导入，不用于目标；
- 目标不支持批量导入，必须显式列出目标名。

#### 模块级符号

每个模块拥有自己的模块级全局变量和私有函数：

- 模块级变量在模块加载时初始化一次；
- 模块级变量只在本模块的函数和目标字段中可见；
- 其他模块只能通过导入的函数间接使用这些变量；
- 导入函数时，模块级变量仍按该模块的环境初始化；
- 导入目标时，目标字段和动作参数在同一模块环境中求值。

#### 目标闭包

导入 `common.core` 时：

- `core` 以及它在同一模块内依赖的目标自动加入计划；
- 这些私有依赖不自动获得 `common.*` 别名；
- 调用方只能引用显式导入的目标；
- 跨模块依赖必须由目标所在模块自己通过 `@import target` 建立；
- 导入目标不会自动重新导出其依赖。

#### 递归与循环

- 模块导入递归解析；
- 同一模块只加载一次；
- 直接或间接循环导入报错，错误包含完整导入链；
- 导入循环在 Configure 阶段、目标求值之前检测；
- 函数导入和目标导入共享同一模块加载状态，但符号暴露规则独立。

#### 模块评估顺序

1. 解析导入声明，建立模块依赖图；
2. 检测循环；
3. 按依赖顺序加载模块；
4. 初始化模块级变量；
5. 语义检查所有声明；
6. 对当前选中目标闭包涉及的目标字段求值；
7. 未导入的目标不加入计划，也不执行目标字段求值。

目标身份仍由 `module + name` 组成，别名只影响源码中的引用写法，不进入稳定身份。

模块表、模块级变量和模块 AST 由 Program/Context 持有；Plan 只保存复制后的目标、动作和字符串，不借用模块 AST。

## 3. 目标模型

### 3.1 声明

目标名使用 C 标识符，不使用字符串：

```c
target app() {
    list<target_ref> depends_on = {core, utils};
    list<string> inputs = {"src/main.c"};
    list<string> outputs = {"build/app"};
    list<action> actions = {
        mkdir("build"),
        command("cc", {"-O2", "src/main.c", "-o", "build/app"})
    };
}
```

- `target` 是声明，不是普通表达式；
- 目标名后使用空参数列表 `()`，表示这是一个无参数、按名字选择的构建单元；
- 目标字段是 C 风格的 `list<T>` 声明：`list<T> 名称 = { ... };`
- 列表字面量使用花括号 `{ ... }`，不使用方括号；
- 目标字段类型固定为 `list<target_ref>`、`list<string>`、`list<action>`；
- 默认目标名为 `main`；
- `-t` 省略时选择 `main`；
- `-t name` 选择一个目标并构建其依赖闭包；`name` 可以是根模块目标名或 `别名.目标名`；
- 目标名在所属模块内唯一；跨模块引用必须通过 `@import target ... as 别名`，写作 `别名.目标名`；
- 根模块的 `main()` 是默认入口；导入模块的 `main()` 不自动成为入口；
- 目标可以只有 `depends_on`，作为聚合目标；
- 未知字段、重复字段、类型不匹配在语义分析阶段报错。

### 3.2 字段

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `depends_on` | `list<target_ref>` | 否 | 依赖目标 |
| `inputs` | `list<string>` | 否 | 输入文件 |
| `outputs` | `list<string>` | 否 | 输出文件 |
| `actions` | `list<action>` | 否 | 按顺序执行的动作 |

未知字段、重复字段、非法类型均报错。

`depends_on` 的元素是目标引用，不是字符串或变量；目标引用保持静态。本模块目标直接写名字，跨模块目标写 `别名.目标名`，且该目标必须已通过 `@import target` 显式导入。

### 3.3 动作

```text
command(program, args [, cwd [, env]])
mkdir(path)
copy(from, to)
move(from, to)
remove(path)
```

- `command` 的参数是 `program` 字符串 + `args` 列表；
- `args` 使用 C 风格列表字面量：`{"-O2", "-c", "main.c"}`；
- `cwd` 相对于工作目录；
- `env` 是 `KEY=VALUE` 字符串列表，在继承环境上覆盖；
- 不支持隐式 shell、管道、重定向；
- 需要 shell 语义时显式调用 `sh` / `cmd`，由调用者承担风险；
- 文件动作路径在 Configure 阶段规范化并冻结。

### 3.4 目标声明的位置

目标名是静态标识符，类比 C 函数名：

```c
target app() {
    list<string> outputs = {"build/app"};
    list<action> actions = {
        command("cc", {"-O2", "src/main.c", "-o", "build/app"})
    };
}
```

- `target` 可以出现在顶层，或 Configure 阶段执行的 `if` / `else` 块内；
- 不允许出现在函数、循环或动作列表内；
- 同名目标只能声明一次；
- 条件分支可以决定目标是否存在；
- 目标字段可以调用函数，也可以读取 `@{...}` 配置；
- 动作列表可以调用函数，由函数内部的循环/条件生成具体动作，但目标名本身保持静态；
- 所有 `target` 声明都在 Configure 阶段求值；Build 阶段不再新增、删除或修改目标。

这样既保留 Configure 阶段的完整计算能力，又让目标身份和目标引用在语法上可静态检查。

## 4. Configure 与 Build

```text
Configure
  ├─ 加载 .ccm 配置
  ├─ Lexer / Parser
  ├─ Semantic Analysis
  ├─ 执行配置期表达式与函数调用
  │    └─ target 声明写入 Plan Builder
  ├─ Lowering
  └─ Plan Validation
        → 构建计划冻结

Build
  ├─ 增量分析
  ├─ Scheduler
  ├─ Executor
  └─ 结果汇总
```

### 4.1 Configure

- 执行配置期表达式、函数和条件；
- 不允许执行外部命令；
- 构建配置（目标平台、工具链、编译器、编译选项）来自 `.ccm`；
- 当前执行主机的系统信息来自只读 `host_info`；
- 两类信息不隐式互相覆盖，交叉编译时目标平台与宿主系统分别保存；
- 目标声明由 Plan Builder 收集；
- 结束时生成 `CMKPlan`；
- `CMKPlan` 冻结后不再执行脚本语句。

### 4.2 Build

- 只消费 `CMKPlan`；
- Scheduler 决定顺序与并发；
- Executor 执行 `action`；
- 不重新解释 `.cmk`；
- 不允许动态修改任务图。

这样既保留了 Configure 的图灵完备能力，也保证 Build 阶段的任务图稳定、可分析、可增量。

### 4.3 入口与使用手感

- 用户只执行一次 `ccmake -c ... -f ...`；
- 默认入口是目标 `main`，`-t name` 选择其他目标；
- 顶层只允许 `@import`、类型/函数/全局常量声明和 `target` 声明；
- 顶层的 `if` 只用于选择目标声明，不执行外部命令、不执行构建动作；
- Configure 不是需要用户手动执行的第二阶段，而是本次 `ccmake` 调用中的“解析与计划”阶段；
- `-v` 输出中明确显示 Configure / Build 分界；
- Build 阶段不重新解释脚本，不会出现 `main` 已结束又执行脚本的情况。

### 4.4 错误阶段

| 阶段 | 错误来源 | 结果 |
|---|---|---|
| Configure | `error()`、语义错误、配置缺失、计划验证失败 | 终止 Configure，不生成可执行 Plan |
| Build | 进程退出失败、文件动作失败 | 当前目标 `FAILED`，由 Scheduler 传播 |
| Build | 用户中断或 fail-fast 取消 | 相关目标 `CANCELLED`，终止并回收子进程 |

`error()` 只在 Configure 阶段执行。Build 阶段不重新解释脚本，因此不存在脚本层 `error()` 的重复执行；Build 失败统一表示为任务状态。

## 5. IR 与所有权

### 5.1 身份

```c
typedef struct cmk_target_key {
    CMKString module;  /* 规范化模块路径 */
    CMKString name;    /* 目标名 */
} CMKTargetKey;
```

- 稳定身份 = `module + name`；
- 运行时数组下标只用于索引，不进入缓存键；
- 目标依赖保存稳定身份或解析后的目标 ID；
- 动作 ID 在 Plan 内稳定。

### 5.2 结构

```c
typedef enum cmk_action_type {
    CMK_ACTION_EXEC,
    CMK_ACTION_COPY,
    CMK_ACTION_MOVE,
    CMK_ACTION_REMOVE,
    CMK_ACTION_MKDIR
} CMKActionType;

typedef struct cmk_process_spec {
    const char *program;
    char **argv;
    char **envp;
    const char *working_dir;
} CMKProcessSpec;

typedef struct cmk_action {
    CMKActionType type;
    union {
        CMKProcessSpec process;
        struct { const char *from; const char *to; } file;
        struct { const char *path; } path;
    } as;
} CMKAction;

typedef struct cmk_target {
    CMKTargetKey key;
    CCDyn dependencies; /* CMKTargetId[] */
    CCDyn actions;      /* CMKAction[] */

    CCDyn inputs;       /* CMKString[] */
    CCDyn outputs;      /* CMKString[] */
    CMKTargetState state;
} CMKTarget;

typedef struct cmk_plan {
    CCDyn targets;      /* CMKTarget[] */
    CCDyn order;        /* 拓扑序 */
    CMKTargetId root;   /* main 或 -t 指定目标 */
    CMKStringArena strings;
} CMKPlan;
```

- `CMKPlan` 拥有所有目标、动作、argv、env、路径和字符串；
- AST 在 Lowering 后可以释放；
- IR 复制所需字符串，不借用 AST 或脚本变量；
- 查询返回借用字符串，修改 Plan 后失效。

### 5.3 计划验证

冻结前必须检查：

- 目标名重复；
- 依赖不存在；
- 依赖环；
- 输出文件重复；
- 动作参数非法；
- 空动作聚合目标；
- `main` 或 `-t` 目标不存在；
- 路径非法。

## 6. Scheduler

### 6.1 状态

```text
PENDING
READY
RUNNING
SUCCESS
FAILED
BLOCKED
SKIPPED
CANCELLED
```

| 状态 | 含义 |
|---|---|
| `PENDING` | 依赖未完成 |
| `READY` | 依赖全部成功，可调度 |
| `RUNNING` | 已分配给 Executor |
| `SUCCESS` | 自身动作全部成功 |
| `FAILED` | 自身动作失败 |
| `BLOCKED` | 依赖失败或被取消 |
| `SKIPPED` | 增量分析判定无需执行 |
| `CANCELLED` | 用户中断或 fail-fast 取消 |

### 6.2 策略

- 默认 fail-fast：首个任务失败后取消 READY/RUNNING 中不相关任务；
- `-k/--keep-going`：继续执行不依赖失败目标的独立任务；
- 依赖失败的目标进入 `BLOCKED`；
- 聚合目标在所有依赖成功后 `SUCCESS`，否则 `BLOCKED`；
- `-j` 控制 READY 队列并发；
- 第一版 Scheduler 单线程，后续把 READY 交给线程池；
- Scheduler 不直接创建进程，只调用 Executor 接口。

### 6.3 选择目标

- `-t` 省略：`main`；
- `-t name`：指定目标；
- 只构建目标依赖闭包；
- 不自动构建未被选中的孤立目标；
- `main` 不存在且未给 `-t` 时报配置错误。

## 7. Executor 与 Process

### 7.1 Process 接口

```c
CMKProcess *cmk_process_create(const CMKProcessSpec *spec);
CCBOOL cmk_process_start(CMKProcess *proc);
CMKProcessResult cmk_process_wait(CMKProcess *proc);
void cmk_process_terminate(CMKProcess *proc);
void cmk_process_destroy(CMKProcess *proc);
```

- 创建、启动、等待、终止、销毁成对；
- Executor 是进程句柄的唯一所有者；
- Scheduler 只接收 `CMKProcessResult`；
- 支持 Windows 与 POSIX 后端；
- argv 直接传给 `CreateProcess` / `execvp`，不经过 shell；
- 进程组/Job Object 负责取消时回收子进程树；
- stdout/stderr 默认流式输出；需要捕获时由 Executor 动态缓冲，内存不足返回明确错误；
- 退出码、信号、超时分别表示。

### 7.2 Executor 职责

1. 从 Scheduler 接收一个目标；
2. 按顺序执行 `actions`；
3. 文件动作直接在宿主文件系统执行；
4. 进程动作创建、等待、回收进程；
5. 返回结构化结果；
6. 不修改其他目标状态；
7. 不决定调度顺序。

## 8. 增量构建

增量构建作为独立模块，不放进 Parser 或 Executor。

任务签名建议：

```text
signature = Hash(
    input_hashes,
    output_paths,
    command_program,
    command_argv,
    command_env_subset,
    command_cwd,
    toolchain_version,
    dependency_output_hashes,
    config_hash,
    defines_hash
)
```

- 使用内容哈希，不依赖 mtime；
- 输出缺失或输出哈希不符则失效；
- 环境变量只纳入显式读取或动作声明的部分；
- 缓存文件带版本号和原子写；
- `-B` 绕过缓存；
- 增量模块可以在 M8 接入，IR 从 M5 就保留字段。

## 9. 模块与文件布局

```text
src/cmk/
  cmk_program.c/h       源码、AST arena、IR arena、诊断
  cmk_source.c/h        源码缓冲区和位置
  cmk_token.h
  cmk_lexer.c/h
  cmk_ast.c/h
  cmk_limits.c/h
  cmk_parser.h
  cmk_parser_internal.h
  cmk_parser.c           Parser 公共入口、Token 缓冲、诊断、事务
  cmk_parser_expr.c      Pratt 表达式解析
  cmk_parser_stmt.c      语句解析
  cmk_parser_decl.c      声明、导入、target 解析
  cmk_diagnostic.c/h
  cmk_semantic.c/h
  cmk_scope.c/h
  cmk_value.c/h
  cmk_builtin.c/h        注册表、签名、调用上下文
  cmk_builtin_std.c      标准内建与动作构造器
  cmk_ir.c/h
  cmk_target.c/h
  cmk_scheduler.c/h
  cmk_process.c/h
  cmk_executor.c/h
  cmk_cache.c/h
```

- 构建脚本递归编译 `src/`，`src/cmk/` 无需修改构建入口；
- 测试放 `tests/cmk/`，并加入 `scripts/build_test.*` 的测试目录；
- 不一次性创建所有文件，按里程碑逐步落地；
- `cmk_program` 或 `cmk_context` 是源码、AST、IR、诊断、值 arena 的根所有者；
- M2 的 AST 数据模型、Parser 拆分、所有权与预算见 [CMK AST 与解析设计](CMK%20AST与解析设计.md)。

## 10. 资源与失败

CMK 不设置语言层的循环次数、递归次数、token 数量、AST 节点数量、目标数量或列表长度配额。

Parser 提供可配置的实现资源预算（`CMKLimits`）：计数类预算默认 0，只受内存约束；语法/表达式递归深度默认开启，用于保护宿主调用栈。该预算不属于语言语义限制，设为 0 即可关闭。详见 [CMK AST 与解析设计](CMK%20AST与解析设计.md) 第 6 节。

- 循环按条件执行，由脚本自身决定终止；
- 递归使用动态调用帧，只受可用内存和宿主资源约束；
- 源码、AST、目标、动作和字符串按需分配；
- 分配失败返回明确的资源错误，不静默截断；
- 无限循环是语言允许的程序，由用户中断或外部终止处理；
- `find_files` 使用已访问集合避免文件系统循环，不固定截断结果；
- stdout/stderr 默认流式处理，捕获时动态缓冲；
- `-j` 是并发控制参数，不是语言能力限制。

## 11. 实施顺序

| 阶段 | 工作 | 验收 |
|---|---|---|
| M0 | 语言规范、EBNF、样例、诊断、所有权表、内建 ABI | 语法、目标模型和内建接口冻结 |
| M1 | Lexer | Token 序列、位置、长度和错误测试 |
| M2 | AST / Parser（见 [CMK AST 与解析设计](CMK%20AST与解析设计.md)） | AST 所有权与校验、表达式优先级、完整样例 AST golden test |
| M3 | Scope / Semantic | 名称、类型、函数、内建签名、目标引用错误 |
| M4 | Value / Configure 求值 | 表达式、函数、内建调用、while 可运行；不执行外部命令 |
| M5 | Lowering / IR / Plan Validation | 生成可冻结计划，AST 可释放 |
| M6 | 单线程 Scheduler + fake Executor | 依赖、失败、BLOCKED、-k 正确 |
| M7 | Process / Executor | 真实命令、文件动作、取消、回收 |
| M8 | 增量缓存 | 签名、缺失输出、-B |
| M9 | 并发调度 | `-j`、状态一致、日志确定 |

每个阶段先写独立测试，再实现功能；不得跨阶段合并。

## 12. 测试边界

| 模块 | 独立测试 | 禁止依赖 |
|---|---|---|
| Lexer | Token 序列 | Parser |
| Parser | AST 结构 | 运行时 |
| Semantic | 名称/类型/目标诊断 | Executor |
| Value | 运算、复制、释放 | 文件系统 |
| Builtin | 注册、签名、参数/返回、错误 | Parser |
| IR | 目标、动作、稳定身份 | 真实进程 |
| Scheduler | 状态机 | 操作系统 |
| Process | 参数、退出码、输出 | 构建图 |
| Executor | 动作执行 | Parser |
| Cache | 命中、失效、版本 | Parser |

Scheduler 测试使用 fake Executor，不启动真实进程。

## 13. 结论

`.cmk` 的核心竞争力不是“尽可能像通用脚本”，而是：

- 用 Turing 完备的语言做 Configure；
- 用稳定的 IR 表达构建计划；
- 用独立 Scheduler 和 Executor 执行；
- 用明确所有权贯穿 AST、IR、值、任务、进程和缓存。

这条路线支持通用项目流程管理，同时不让 Parser 和执行器互相纠缠。
