# CMK 语义与运行时设计

本文冻结 M3（Scope / Semantic）与 M4（Value / Configure 求值）的模块边界、数据模型、求值规则与验收范围。M3 只回答“名字和类型是否合法”，M4 只回答“合法的 Configure 程序如何求值”；两者都不生成 Build Plan，不执行外部命令。

M3/M4 状态：已完成。M5-M9 已落地（IR/Plan、Scheduler、Process/Executor、Cache、并发调度），CLI 已串接 Configure → Lower → Scheduler → Executor。实现文件为 `cmk_value.*`、`cmk_config.*`、`cmk_builtin.*`、`cmk_builtin_std.c`、`cmk_semantic.*`、`cmk_runtime.*`、`cmk_program.*`，以及 `cmk_target.*`、`cmk_ir.*`、`cmk_scheduler.*`、`cmk_process.*`、`cmk_executor.*`、`cmk_cache.*`。

## 1. 阶段目标与非目标

### 1.1 M3

- 建立模块、函数、块、循环与 target 的作用域；
- 检查变量、函数、内建函数、target 字段、配置引用与 target 引用的名称；
- 检查类型、参数数量、返回类型、const、重复声明与 target 字段结构；
- 解析配置引用路径并绑定配置树类型；
- 解析 `@import` 暴露的函数与 target；
- 为 M4 保存表达式类型、调用绑定和 target 引用绑定。

### 1.2 M4

- 实现值模型、深复制、列表/字符串/动作/目标引用所有权；
- 求值算术、比较、逻辑、调用、索引、配置引用与 target 引用；
- 执行变量声明、赋值、块、if、while、for-in、return、break、continue；
- 支持用户函数、递归、跨模块函数调用；
- 执行标准内建与动作构造器；
- 求值根模块的 target 声明，生成 M4 的 target draft；
- 提供 `.cmk` 模块加载与 Configure 入口。

### 1.3 非目标

- 不生成 `CMKPlan`、不做 Lowering、不做 Plan Validation；
- 不创建进程，不执行 `command` 动作；
- 不做 Scheduler、Executor、增量缓存；
- 不做闭包、函数指针、匿名函数、动态调用；
- 不把 target draft 冻结为稳定 IR；M5 才拥有 Plan。

## 2. 模块边界

| 模块 | 主要文件 | 可以依赖 | 禁止依赖 |
|---|---|---|---|
| Value | `cmk_value.h/.c` | CrystalStd、`ccalloc`、`cc_dyn` | Parser、AST、Runtime、文件系统 |
| Config | `cmk_config.h/.c` | `ccvar.h`、Value | Parser、Semantic、Runtime |
| Builtin | `cmk_builtin.h/.c`、`cmk_builtin_std.c` | Value、Diagnostic、Runtime 声明 | Parser、AST、IR |
| Semantic | `cmk_semantic.h/.c` | AST、Config、Builtin、Value | Runtime、Plan、Executor、真实进程 |
| Runtime | `cmk_runtime.h/.c` | AST、Semantic、Config、Builtin、Value | Parser 实现、Plan、Scheduler |
| Program | `cmk_program.h/.c` | 上述模块、`.ccm` 定位/解析、路径工具 | Plan、Scheduler、Executor |

数据流：

```text
.cmk 文件
  → Program：定位/读取/解析、导入图
  → Semantic：作用域、类型、绑定
  → Runtime：值、函数调用、Configure 求值
  → CMKTargetDraft（M4 终点）
  → M5 Lowering / Plan
```

- Program 拥有 `CMKAST`、`CMKSemanticModule`、`CMKRuntime` 与模块表；
- Semantic 借用 AST、Config、Builtin 注册表与诊断对象；
- Runtime 借用 Semantic、Config、Builtin 与诊断对象；
- AST 在 Runtime 完成前不释放；M5 生成 Plan 后才可释放；
- Value 不依赖 AST 节点 ID，不借用 AST 字符串；
- Builtin 处理函数通过 `CMKBuiltinCall` 访问 Runtime，不接触 Parser/AST。

## 3. Value 模型

### 3.1 类型

```c
typedef enum cmk_value_type {
    CMK_VALUE_VOID = 0,
    CMK_VALUE_INT,
    CMK_VALUE_FLOAT,
    CMK_VALUE_BOOL,
    CMK_VALUE_STRING,
    CMK_VALUE_LIST,
    CMK_VALUE_ACTION,
    CMK_VALUE_TARGET_REF,
    CMK_VALUE_TYPE_COUNT
} CMKValueType;

typedef struct cmk_type {
    CMKValueType kind;
    CMKValueType element;   /* 仅 list 使用 */
} CMKType;
```

- 语言层不允许嵌套 `list`；`list` 的 `element` 必须是标量/`action`/`target_ref`；
- 空列表在上下文确定前为 `list<void>`，Semantic/Runtime 必须在赋值、实参、返回值、target 字段或配置引用上下文中解析；
- `target_ref` 只在 target 的 `depends_on` 字段内合法；
- `void` 只作为函数返回类型；变量/参数不能是 `void`。

### 3.2 值所有权

```c
typedef struct cmk_value {
    CMKValueType type;
    union {
        CCINT64 integer;
        double floating;
        CCBOOL boolean;
        CMKString string;
        CMKList *list;
        CMKAction *action;
        struct { CMKString module; CMKString name; } target_ref;
    } as;
} CMKValue;
```

- 每个 `CMKValue` 拥有自己的字符串、列表、动作；
- `cmk_value_copy` 深复制；`cmk_value_clear` 释放并归零；
- 列表元素按值存储；追加时复制；
- 动作拥有 program、argv、env、路径字符串；
- 目标引用拥有模块路径与目标名；
- Runtime 的环境变量、函数参数、返回值都使用拥有值，不把 AST 指针交给 Plan；
- 失败路径允许部分值存在，由调用方统一 clear。

### 3.3 动作值

```c
typedef enum cmk_action_kind {
    CMK_ACTION_EXEC,
    CMK_ACTION_COPY,
    CMK_ACTION_MOVE,
    CMK_ACTION_REMOVE,
    CMK_ACTION_MKDIR
} CMKActionKind;
```

- `command(program, args [, cwd [, env]])` 生成 `CMK_ACTION_EXEC`；
- `mkdir` / `remove` 生成 path 动作；
- `copy` / `move` 生成 from/to 动作；
- 动作构造只复制参数，不创建进程；
- M5 才把动作值转成 `CMKAction` IR。

## 4. Config 适配

`cmk_config.h` 把 `.ccm` 变量树转换成 CMK 类型/值：

| `.ccm` 类型 | CMK 类型 |
|---|---|
| string | `string` |
| int | `int` |
| float | `float` |
| bool | `bool` |
| array | `list<T>`，要求元素同质 |
| object | 不支持作为表达式值 |

- 空数组返回 `list<void>`，由上下文补全元素类型；
- 数组元素混类型报错；
- 配置路径继续使用 `a.b[0]` 形式，由 `ccvar_get` 直接解析；
- Semantic 查询类型，Runtime 查询值；
- 缺失字段、混合数组、对象值都产生明确诊断；
- `@include` 由 Program 调用 `.ccm` 定位/解析器写入同一个配置树；不再由 Semantic 访问文件系统。

## 5. Builtin 边界

### 5.1 注册表

- `CMKBuiltinRegistry` 拥有注册项的副本；
- 注册时校验 ABI、名称、参数上下界、函数指针；
- 重名注册失败，注册表保持原状；
- 内建名全局唯一，用户函数不能与内建同名；
- 注册表初始化后只读。

> 与早期草案的差异：实现使用 `CMKBuiltinRegistry` 对象（由 Program 拥有），不再使用全局 `cmk_builtin_init/shutdown`；注册表复制 `CMKBuiltinSpec`，注册项与参数表可来自静态数据。Value 的 setter 不接收 Runtime 参数，内存所有权由 Value 自身管理。

### 5.2 标准内建

| 类别 | 函数 | 状态 |
|---|---|---|
| 纯计算 | `len`、`join`、`subst`、`basename`、`dirname` | M4 可用 |
| 只读查询 | `host_info` | M4 可用（`os`/`arch`/`cpu_count`/`path_separator`） |
| 只读查询 | `find_files` | M4 可用：递归只读遍历并排序去重 |
| 动作构造 | `command`、`mkdir`、`copy`、`move`、`remove` | M4 可用，返回动作值 |
| 诊断 | `info`、`warn`、`error` | M4 可用；`error` 标记 fatal |

- `info`/`warn` 直接输出日志；`error` 写诊断并终止 Configure；
- 格式化支持 `%s`、`%d`、`%f`、`%v`、`%%`；
- Builtin 参数类型由 `CMKBuiltinArg` 给出；`list<void>` 表示任意列表；
- Builtin 返回类型由 Runtime 复核；
- 处理函数不保存参数指针，不释放借用值。

## 6. Semantic

### 6.1 作用域

```text
module scope
  ├─ module function symbols
  ├─ module global variables
  └─ imported function symbols
function scope
  └─ parameters
block scope
  └─ local variables
```

- 模块作用域包含函数与模块级变量；
- 函数作用域父级是模块作用域；函数体块不额外创建作用域，参数与函数体顶层局部变量同层（重复声明报错）；
- 普通嵌套块创建子作用域，允许遮蔽外层变量；
- target 不在普通表达式命名空间中，只通过 target 引用规则解析；
- 导入函数进入当前模块作用域；导入 target 通过 `别名.目标名` 解析。

### 6.2 名称与类型检查

- 未定义变量/函数/内建/target 引用报错；
- 函数不能作为值；target 不能当变量或函数；
- 变量、函数、参数重复声明报错；
- `const` 变量不能再次赋值；
- 变量初始化、赋值、`+=`、函数实参、返回值、target 字段值做类型匹配；
- `+` 支持 int/int、float/float、string/string、list/list（元素类型一致）；
- `- * /` 要求同类型数值；`%` 只支持 int；
- `== !=` 支持同类型标量；大小比较支持 int/float/string；
- `&& || !` 要求 bool；
- `list<T>` 索引要求 int，结果是 T；
- 空列表通过上下文解析元素类型；无法解析报错；
- 非 void 函数所有可达控制流必须返回；`error()` 后的路径不需要返回；
- `break`/`continue` 只能在循环内；
- 非 void 函数调用结果必须赋值、作为返回值或在 `_ =` 中使用；表达式语句中的非 void 结果报错。

### 6.3 内建绑定

- 用户函数优先于内建？不：用户函数与内建同名在声明阶段即拒绝；
- 调用节点记录 `CMKFunctionInfo` 或 `CMKBuiltinSpec`；
- 参数个数、必选/可选参数和类型由 Semantic 检查；
- `CMKBuiltinCall` 的运行时错误由 Builtin 写诊断，Runtime 负责 fatal 状态。

### 6.4 target

- target 名在无条件声明中唯一；顶层 if 的不同条件分支允许同名 target，运行时只选择一个；
- 四个字段：`depends_on` / `inputs` / `outputs` / `actions`；
- 字段类型严格为：
  - `list<target_ref>`
  - `list<string>`
  - `list<string>`
  - `list<action>`
- 未知字段、重复字段、错误类型报错；
- `depends_on` 内允许目标引用：
  - 本模块 `target` 直接写名字；
  - 跨模块写 `别名.目标名`，别名必须来自 `@import target ... as`；
- target 字段可调用函数、读取配置、使用模块全局变量；
- 函数/循环中不能声明 target；普通块不能包含 target。

### 6.5 导入

- `@import func`：按函数名导入，支持别名与 `*`；
- `@import target`：必须列出目标并绑定模块别名；
- 函数与目标命名空间独立；
- 导入模块递归加载；同一路径只加载一次；
- 循环导入报错；
- 被导入模块先完成语义分析，再为当前模块建立导入符号；
- 导入函数进入当前模块作用域；导入 target 不进入普通表达式命名空间；
- 目标引用只使用当前模块显式导入的 target，不自动重新导出。

## 7. Runtime

### 7.1 环境

```c
typedef struct cmk_env_var {
    CMKString name;
    CMKValue value;
    CCBOOL is_const;
} CMKEnvVar;

typedef struct cmk_env {
    struct cmk_env *parent;
    CCDyn vars;
} CMKEnv;
```

- 每个模块有模块环境；
- 函数调用建立函数环境，父级是该函数所属模块的模块环境，**不是调用者环境**（无闭包）；
- 普通块建立子环境；
- target 字段在模块环境中求值；
- 变量查找沿父链；赋值只修改找到的槽；
- 函数参数按值复制到函数环境；
- 调用帧动态分配，语言层不设递归次数配额；宿主栈是实际边界。

### 7.2 求值规则

- 算术与运行时溢出检查；整数除零、`INT64_MIN / -1` 报错；
- 浮点除零报错；
- 字符串 `+` 复制拼接；
- list `+` 复制两个列表的元素；
- `&&` / `||` 短路；
- 索引越界报错；
- `error()` 设置 fatal，后续语句不再执行；
- 函数返回值按声明类型复核；
- 非 void 函数没有命中 return 时运行时报错；
- 跨模块函数调用通过 Program 提供的 module → runtime 映射；被调用模块的全局变量必须先初始化。

### 7.3 target draft

```c
typedef struct cmk_target_draft {
    const CMKTargetInfo *info;
    CMKValue depends_on;
    CMKValue inputs;
    CMKValue outputs;
    CMKValue actions;
} CMKTargetDraft;
```

- Configure 先初始化所有模块级全局变量，再求值根模块 target；
- target draft 是 M4 的临时终点，不是 Plan；
- 同一配置中选中同名 target 报错；
- 未导入的模块 target 不自动求值；
- 导入 target 的闭包、Plan 冻结、目标选择、输出检查属于 M5。

### 7.4 执行策略

```c
typedef struct cmk_execution_policy {
    CCUINT32 max_recursion_depth;
    CCUINT64 max_steps;
    CCUINT64 max_configure_time_ms;
    CCUINT32 max_find_depth;
    CCUINT32 max_processes;
    CCUINT64 max_output_capture;
} CMKExecutionPolicy;
```

- 所有字段默认 0，表示不限制；
- Runtime 当前接入 `max_steps`（表达式/语句步数）、`max_recursion_depth`（函数调用深度）与 `max_configure_time_ms`（`clock()` CPU 时间）；`max_find_depth` 接入 `find_files` 目录栈；
- `CMKProgram` 通过 `cmk_program_set_policy` 在 Configure 前把策略下发到各模块 Runtime；
- `CMKProgram` 创建时读取 `CCMAKE_CMK_MAX_STEPS`、`CCMAKE_CMK_MAX_RECURSION_DEPTH`、`CCMAKE_CMK_MAX_CONFIGURE_TIME_MS`、`CCMAKE_CMK_MAX_FIND_DEPTH`、`CCMAKE_CMK_MAX_PROCESSES`、`CCMAKE_CMK_MAX_OUTPUT_CAPTURE` 环境变量；`max_processes` 未设置时继承 `-j/--jobs`；
- `max_recursion_depth`、执行时间、并发、输出捕获字段保留给后续 VM/Scheduler/Executor；
- 策略失败写诊断并终止 Configure，不发布半成品 target draft。

## 8. Program

- `cmk_program_load`：
  1. 规范化根路径；
  2. 读取/解析模块；
  3. 处理 `@include`（调用现有 `.ccm` 定位/解析器写入配置树）；
  4. 使用显式加载帧栈（`CMKProgramLoadFrame`）扫描 `@import`、递归加载子模块并检测循环；
  5. 子模块加载完成后，为每个模块建立 Semantic，加入导入函数/目标，执行语义分析；
- `cmk_program_configure`：
  1. 为所有已加载模块建立 Runtime；
  2. 按导入依赖递归初始化模块全局变量；
  3. 求值根模块 target 声明，生成 target draft；
- 模块身份使用规范化路径；Windows 路径比较不区分大小写；
- 同一模块只加载/分析一次；
- Program 拥有模块 AST、Semantic、Runtime；
- Program 不生成 Plan，不执行动作。

## 9. 诊断

- Semantic/Runtime 诊断使用节点 span 的文件/行/列；
- 首错终止，保留第一条诊断；
- Parser/Lexer 诊断在 Program 中复制到 Program 诊断对象；
- Builtin 失败写 `CMKBuiltinCall.diagnostic`；
- `error()` 诊断文本由格式化参数生成，Runtime 设置 fatal；
- 最终由 `parse_cmk` 使用 `cmk_diagnostic_print` 输出并返回失败。

## 10. 测试

| 套件 | 覆盖 |
|---|---|
| `CMKVALUE` | 类型、字符串所有权、列表深复制、目标引用、动作、文本化、尺寸运算与动态追加边界 |
| `CMKBUILTIN` | 注册表、纯计算、host_info、动作构造、find_files、error 格式 |
| `CMKSEMANTIC` | 作用域、类型、函数、内建、target、配置、控制流、5000 层显式语句/表达式/顶层 if 栈 |
| `CMKRUNTIME` | 全局变量、while、函数/递归、列表/字符串、target draft、for/break/continue、运行时错误、执行策略（步数/递归/时间/find 深度）、5000 层显式语句/表达式/调用/尾递归/顶层 target 栈、线性/分支递归程序栈、通用语句上下文 VM、5000 层非 return 深递归、循环内递归 |
| `CMKPROGRAM` | 根模块、跨模块函数、target 导入、通配/别名、@include、循环导入、导入错误、Program 执行策略、环境变量策略、并发策略继承、进程/捕获策略、非法环境变量回退 |

M3/M4 完成后的全量验收：Debug/Release 全通过，`-fanalyzer` 无告警，`git diff --check` 通过。

## 11. 已知限制与后续

- `find_files` 当前使用 `CCFiles` 只读遍历；后续可扩展到更完整的跨平台路径规则与符号链接策略；
- M4 只求值根模块 target；导入 target 的闭包与冻结属于 M5；
- M4 不生成 Plan，不做目标选择/输出检查/依赖环检查；
- 分配失败注入与覆盖引导 Fuzz 不纳入当前测试体系；
- 语言层递归无固定配额，实际受宿主栈/内存约束；
- `@include` 与根 `.ccm` 合并使用同一个配置树；同一字段的覆盖顺序按 `.cmk` 源顺序与 `.ccm` 解析顺序确定。
