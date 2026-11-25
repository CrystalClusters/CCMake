# MC 实现细则

`.mc` 文件解析分为两个阶段：第一阶段检查并定位根文件与全部 `@include` 文件，第二阶段解析文件内容。本文描述第一阶段的模块结构、技术选型与实现方式。

第二阶段的模块与解析规则见 [全局变量树](全局变量树.md) 与 [MC内容解析](MC内容解析.md)。

第一阶段的产物是完整文件表、include 边表和后序解析顺序。阶段二读取这些产物，不重新定位 `.mc` 文件；`${find}` 按自身参数访问目录。

## 选型摘要

| 主题 | 选型结果 |
|---|---|
| 文件访问 | 单 VFS 探针，逐文件 `CC_FS_MOUNT_FILE` 挂载到 `/__mc:<序号>`，宿主 `stat` 类型门，读入完整内容快照 |
| 数据结构 | 文件、include 边、解析顺序与 DFS 栈统一使用 `CCDyn` 连续数组；元素之间用下标互引 |
| 查重算法 | 规范化绝对路径为键，文件表线性顺序查找；单次 O(n)，追加摊还 O(1) |
| 图算法 | 显式栈 DFS + 三状态标记，后序生成 `order[]`；边解析时完成环检测 |
| 路径模型 | 规范化绝对宿主路径，`/` 分隔；根路径以 `-C` 为基准，include 路径以包含者目录为基准 |
| include 发现 | 单遍字节状态机，跳过注释与字符串，跟踪括号深度，记录行号与字节列 |
| 内容传递 | 已去 BOM 的内存快照随文件表交给阶段二；阶段二不依赖阶段一的文件定位，`${find}` 使用 VFS 访问目录 |
| 资源管理 | `mc_locate` 返回前释放全部 VFS 节点；`mc_context_clear` 释放内容与数组 |

---

## 1. 模块职责

| 项 | 内容 |
|---|---|
| 输入 | `-c/--config` 指定的根配置文件；`-C/--directory` 指定的工作目录 |
| 输出 | `files[]`：所有被定位文件及其内容快照；`includes[]`：include 边；`order[]`：后序解析顺序 |
| 文件检查 | 路径规范化、工作目录有效性、普通文件类型、可读性、编码标记 |
| 指令发现 | 识别顶层 `@include "..."`，跳过注释与字符串中的同名文本 |
| 图处理 | 路径去重、循环检测、解析顺序生成 |
| 不包含 | 键值语法、字段树、`${ref}`、`${find}`、`-D` 覆盖、`.mk` 解析 |

---

## 2. 模块结构

```text
parse_mc
  └─ mc_locate
       ├─ mc_path   宿主路径规范化、目录提取、路径比较、类型门、当前工作目录
       ├─ mc_scan   @include 指令扫描：注释、字符串、括号深度、行号
       └─ mc_locate VFS 装载、文件注册表、DFS、顺序生成、诊断
```

| 文件 | 职责 |
|---|---|
| `src/mc/mc_internal.h` | 内部数据结构、状态枚举、接口声明 |
| `src/mc/mc_path.c` | 路径规范化、目录提取、路径比较、宿主文件类型、进程工作目录 |
| `src/mc/mc_scan.c` | `mc_scan_includes`：从文件内容提取 include 指令 |
| `src/mc/mc_locate.c` | `mc_locate`：VFS 装载、注册表、DFS、错误诊断、资源清理 |
| `src/mc/mc.c` | `parse_mc` 编排与阶段一结果输出 |

---

## 3. 文件访问：VFS 逐文件挂载

第一阶段使用 CrystalStd 的虚拟文件空间（VFS）访问 `.mc` 文件。每次 `mc_locate` 创建一个 VFS 探针，所有文件共用该探针。

每个被定位文件按以下顺序装载：

```text
宿主 stat 类型门：只接受普通文件
  → 选择一个空闲合成路径 "/__mc:<序号>"
  → ccfiles_mount(fs, v_path, real_path, CC_FS_MOUNT_FILE)
  → ccfiles_stat               读取 size
  → ccfiles_open + ccfiles_read 读入完整内容
  → 内容快照进入文件表
```

合成路径直接位于虚拟根 `/` 下。挂载前先以 `ccfiles_stat` 检查同名节点是否存在，存在则顺延序号。合成名包含 `:`，与 Windows 宿主文件名空间分离；虚拟根下已有节点不会被覆盖。

宿主 `stat` 作为装载前的类型门：目录、FIFO、设备文件和其他非普通文件在进入 VFS 之前被拒绝。VFS 承担文件访问、大小与时间读取、内容读取和生命周期管理。

VFS 探针的工作目录始终保持为 `/`，所有挂载点都使用绝对虚拟路径，文件定位不依赖探针的 cwd 状态。

### 资源释放

`mc_locate` 返回前统一释放 VFS：

1. 对每个已挂载文件执行 `ccfiles_remove(fs, v_path)`，摘除合成虚拟节点；
2. `ccfiles_destroy(fs)` 释放探针；
3. 文件内容快照与路径字符串由调用方通过 `mc_context_clear` 释放。

`ccfiles_remove` 作用于显式挂载锚点时只删除虚拟节点，宿主 `.mc` 文件不受影响。

---

## 4. 路径模型

### 4.1 规范形式

定位过程中使用的宿主路径统一为：

- 绝对路径；
- 使用 `/` 分隔；
- 不含空组件、`.`、`..`；
- 根路径保留结尾 `/`（如 `/`、`D:/`）；
- Windows 盘符大写；
- 保留文件名原始大小写。

### 4.2 路径来源

| 路径 | 基准 |
|---|---|
| 根配置文件（`-c`） | `-C` 工作目录；未指定 `-C` 时使用进程当前工作目录 |
| `@include` 目标 | 包含它的 `.mc` 文件所在目录 |
| 绝对 `@include` 目标 | 直接使用 |

`-C` 按 `make -C` 语义参与根路径解析；阶段一不切换进程工作目录。

### 4.3 规范化步骤

```text
Windows 将 '\' 统一为 '/'
  → 相对路径拼接基准目录
  → 识别 POSIX 根、Windows 盘符根、UNC 前缀
  → 按组件处理：丢弃空组件与 "."，".." 弹栈
  → 重拼并补 0
```

`..` 不越过 POSIX 根、Windows 盘符根以及 UNC 的 server/share 两段。Windows 下组件以空格或点结尾时拒绝，避免 Win32 路径归一化产生错位。

路径缓冲按实际长度分配，不设固定长度上限。

### 4.4 平台规则

| 场景 | 规则 |
|---|---|
| POSIX 绝对路径 | `/...` |
| Windows 绝对路径 | `X:/...`；`/...` 为当前盘根；UNC 为 `//server/share/...` |
| Windows 盘符相对路径 | 拒绝（`X:foo`） |
| 路径比较 | Windows 使用大小写不敏感比较；POSIX 使用字节比较 |
| `~`、环境变量、通配符 | 不展开，按字面路径处理 |
| 软链接 / junction | 不解析；include 基准为打开路径所在目录 |
| 反斜杠 | Windows 在规范化时转换为 `/`；其他平台保留原字节，含反斜杠的宿主路径由 VFS 拒绝 |

---

## 5. `@include` 语法与图

### 5.1 指令形式

```text
@include "<路径>";
```

| 项 | 规则 |
|---|---|
| 引号 | 双引号，路径内不允许未转义双引号 |
| 结束符 | 分号 |
| 位置 | 文件顶层；括号深度必须为 0 |
| 路径 | 字面量；不展开变量、函数、环境变量或通配符 |
| 内容检查 | 目标必须是普通文件、可读、无 NUL、编码合法；扩展名不限 |
| 空白与注释 | token 之间允许空白、行注释和块注释 |

路径按字节拷贝，不做转义处理。

### 5.2 文件注册表

`files[]` 以规范化绝对路径为键。新增 include 目标时：

1. 解析为绝对路径；
2. 在注册表中查找；
3. 已存在时复用该文件项；
4. 不存在时追加文件项并装载。

查找采用文件表上的线性顺序扫描，路径比较使用平台规则：Windows 大小写不敏感，POSIX 大小写敏感。注册表的存储结构与查找算法见 §7.2。

### 5.3 遍历与顺序

使用显式栈的深度优先遍历。每个文件具有 `UNSEEN`、`VISITING`、`VISITED` 三种状态；栈帧、状态转移与复杂度见 §7.4。

文件的所有 include 边处理完毕后，文件进入 `VISITED` 并追加到 `order[]`。因此 `order[]` 是后序序列：

- 每条 include 边的目标都先于包含者；
- 每个文件恰好出现一次；
- 根文件位于序列末尾。

后序序列直接表达“被包含文件先于当前文件解析”的语义。

### 5.4 重复与循环

同一路径首次出现时装载并遍历；再次出现时复用文件项，不重复解析。该语义使菱形依赖中的公共文件只应用一次。

目标文件处于 `VISITING` 状态时构成循环导入。定位立即失败，诊断输出完整循环链：

```text
配置文件定位失败：<文件>:<行>: 检测到循环导入：<A> -> <B> -> <A>
```

### 5.5 根文件

根文件以 `-c` 路径为输入，按 `-C` 基准解析为绝对路径，检查普通文件与编码后进入注册表；扩展名不参与判定。根文件的 `parent` 为空，`depth` 为 0，`order[]` 的最后一项指向根文件。

---

## 6. include 扫描

`mc_scan_includes` 对文件内容做单遍字节扫描，只提取 `@include` 指令。

### 6.1 状态

| 状态 | 行为 |
|---|---|
| 普通 | 识别注释、字符串、括号、`@` 指令 |
| 行注释 | 到换行结束 |
| 块注释 | 到 `*/` 结束，不嵌套 |
| 字符串 | 到下一个双引号结束 |

普通状态下：

- `{`、`[` 增加括号深度；
- `}`、`]` 减少括号深度；
- `@` 后读取标识符，等于 `include` 且深度为 0 时解析指令；
- 其他 `@` 指令不处理，留给内容解析阶段。

每条 include 边记录：

- 原始路径字符串；
- 行号（1 基）；
- 字节列号（1 基）。

### 6.2 编码处理

| 输入 | 处理 |
|---|---|
| UTF-8 BOM | 跳过，内容快照不含 BOM |
| UTF-16 / UTF-32 BOM | 报错并终止定位 |
| NUL 字节 | 报错，视为二进制内容 |
| CRLF / 单独 CR | 按一次换行计行 |
| 其他字节 | 原样跳过，不强制 UTF-8 |

扫描器是字节状态机，语法只依赖 ASCII 结构字符；中文注释、GBK 字符串等非 ASCII 内容按字节透传。

### 6.3 报错范围

第一阶段报告与 include 发现直接相关的词法错误：

- `@include` 不在顶层；
- `@include` 后缺少双引号路径；
- 路径为空或含控制字符；
- 路径字符串未闭合；
- 语句缺少分号；
- 字符串或块注释未闭合。

键值语法、未知 `@` 指令、括号配对等由内容解析阶段处理。

---

## 7. 数据结构与算法

### 7.1 存储基座：CCDyn 连续数组

```c
typedef enum {
    CC_MC_FILE_UNSEEN = 0,
    CC_MC_FILE_VISITING,
    CC_MC_FILE_VISITED
} CC_MC_FILE_STATE;

typedef struct ccmake_mc_include_struct {
    char     *literal;    /* @include 原始路径 */
    CCUINT32  line;       /* @include 行号 */
    CCUINT32  column;     /* @include 列号 */
    CCUINT32  child;      /* 目标文件下标；CC_MC_NIL 表示未解析 */
} CCMakeMCInclude;

typedef struct ccmake_mc_file_struct {
    char     *real_path;     /* 绝对宿主路径 */
    char     *v_path;        /* VFS 合成挂载点 */
    char     *content;       /* 内容快照 + 结尾 0 */
    CCUINT64  size;          /* 内容字节数 */
    CCUINT32  include_first; /* 边表起始下标 */
    CCUINT32  include_count; /* 边数量 */
    CCUINT32  parent;        /* 首次包含者下标；根为 CC_MC_NIL */
    CCUINT32  via_line;      /* 在 parent 中的 include 行号 */
    CCUINT32  depth;         /* include 深度，根为 0 */
    CC_MC_FILE_STATE state;
    CCBOOL    mounted;
} CCMakeMCFile;

typedef struct ccmake_mc_context_struct {
    CCFiles *fs;          /* VFS 探针；mc_locate 返回前释放 */
    CCDyn    files;       /* CCMakeMCFile[] */
    CCDyn    includes;    /* CCMakeMCInclude[] */
    CCDyn    order;       /* CCUINT32[]，后序解析顺序 */
    CCDyn    stack;       /* DFS 帧 */
    char    *work_dir;    /* 规范化绝对工作目录 */
    CCUINT32 root_index;  /* 根文件下标 */
    CCUINT64 total_bytes; /* 已装载文件内容总字节 */
    CCMakeMCLimits limits;
} CCMakeMCContext;
```

文件与边存储在 `CCDyn` 中，元素之间使用下标互相引用。`CCDyn` 扩容会搬移元素内存，遍历与访问始终通过下标完成，不跨扩容持有元素指针。`CCMakeMCLimits` 的字段与默认值见 §7.8。

文件、include 边、解析顺序与 DFS 栈都以 `CCDyn` 连续数组承载：

| 数组 | 元素 | 访问方式 |
|---|---|---|
| `files` | `CCMakeMCFile` | 文件下标 |
| `includes` | `CCMakeMCInclude` | 边下标 |
| `order` | `CCUINT32` | 顺序位置 |
| `stack` | `CCMakeMCFrame` | 栈顶位置 |

数组元素按值连续存放，路径、内容等变长数据另行分配并由元素持有指针。尾部追加使用 `cc_dyn_core_push`，摊还 O(1)；按下标读取为 O(1)。文件与文件之间、文件与边之间只保存 `CCUINT32` 下标，`CC_MC_NIL` 表示空。`CCDyn` 扩容搬移元素后下标保持不变，因此数组项之间可以安全地长期互引。

### 7.2 文件注册表：规范化路径键 + 线性顺序查找

文件注册表由 `files` 数组本身承担，不维护额外的索引结构。查重键是 `CCMakeMCFile.real_path`，即经过 `mc_path_normalize` 处理后的绝对宿主路径：`/` 分隔、`.` 与 `..` 已解析、空组件已合并、Windows 盘符已大写。

查重采用线性顺序查找，从下标 0 开始按登记顺序比较：

```c
static CCUINT32 mc_find_file(CCMakeMCContext *ctx, const char *real_path)
{
    CCUINT32 count = file_count(ctx);
    for (CCUINT32 i = 0; i < count; i++)
    {
        if (mc_path_equal(file_at(ctx, i)->real_path, real_path))
            return i;
    }
    return CC_MC_NIL;
}
```

`mc_path_equal` 按平台规则比较：

| 平台 | 比较方式 | 大小写 |
|---|---|---|
| POSIX | `strcmp` 逐字节比较 | 敏感 |
| Windows | `_stricmp` | 不敏感 |

设文件数为 n、路径长度为 L：

| 操作 | 复杂度 |
|---|---|
| 单次查重 | O(n·L) |
| 追加新文件 | 摊还 O(1) + 路径拷贝 O(L) |
| 重复命中 | 直接复用已登记下标，不新增文件项 |

每条 include 边解析后执行一次查重；设 include 边数为 m，则查重总开销为 O((n + m)·n·L)。配置级项目的文件数量为几十个，线性查找的路径比较次数与文件登记顺序一致，首个命中即为目标。

### 7.3 include 边表：连续区间存储

所有 include 边按文件扫描顺序连续存放在 `includes` 数组中。每个文件通过两个字段描述自己的边区间：

```c
CCUINT32 include_first; /* 本文件第一条边在 includes 中的下标 */
CCUINT32 include_count; /* 本文件边数量 */
```

第 k 条边（k 从 0 起）的下标为 `include_first + k`，访问为 O(1)。扫描一个文件时把新边连续追加到 `includes` 尾部，扫描结束后记录起始下标与数量；文件之间不通过链表串联，边表保持连续内存布局。

边表元素为 `CCMakeMCInclude`，保存原始路径、规范化路径、行号、列号与目标文件下标。追加边的摊还复杂度为 O(1)，m 条边的总存储与扫描开销为 O(m)。

### 7.4 DFS：显式栈 + 三状态标记 + 后序

图遍历使用显式栈，栈帧记录当前文件下标与下一条待处理边的序号：

```c
typedef struct ccmake_mc_frame_struct {
    CCUINT32 file;
    CCUINT32 next;
} CCMakeMCFrame;
```

文件状态为：

| 状态 | 含义 |
|---|---|
| `UNSEEN` | 已登记，未装载 |
| `VISITING` | 位于 DFS 栈上，正在处理其 include |
| `VISITED` | 已装载，已追加到 `order[]` |

遍历按以下规则推进：

1. 取栈顶文件的第 `next` 条边；`next` 自增并写回栈帧；
2. 解析边目标路径并查重；
3. 目标为 `VISITING` 时报告循环导入；
4. 目标为 `VISITED` 时复用，不重复遍历；
5. 目标为 `UNSEEN` 时装载、标记 `VISITING`、压栈；
6. 文件所有边处理完毕后标记 `VISITED`，追加到 `order[]`，弹栈。

每个文件入栈一次、出栈一次，每条边处理一次，结构性遍历开销为 O(n + m)。`order[]` 的尾部追加为摊还 O(1)，n 个文件生成顺序的总开销为 O(n)。显式栈的最大深度为 include 深度，最坏为 n。

循环本体：

```text
root 装载 → state=VISITING → push({root, 0})

while stack 非空:
    frame = top
    if frame.next < file.include_count:
        edge = includes[file.include_first + frame.next]
        frame.next++
        set_top(frame)
        resolved = normalize(edge.literal, dirname(file.real_path))
        file_node = find_file(resolved)

        if file_node 已登记:
            edge.child = file_node
            if file_node.state == VISITING:
                报告循环导入
                fail
            continue

        file_node = append_file(resolved, ...)
        load_file(file_node)
        state[file_node] = VISITING
        edge.child = file_node
        push({file_node, 0})
        continue

    state[file] = VISITED
    order.push(file)
    pop()
```

伪代码中的 `continue` 都回到栈顶检查；只有文件所有 include 边处理完才追加 `order[]` 并弹栈，因此形成后序。

后序追加保证每条边的目标先于包含者进入 `order[]`，根文件最后进入。

### 7.5 路径规范化：组件栈 + 动态缓冲

`mc_path_normalize` 在单块可变缓冲上完成规范化：

1. Windows 将 `\` 统一为 `/`；
2. 相对路径拼接基准目录；
3. 识别 POSIX 根、Windows 盘符根或 UNC 前缀；
4. 从左到右切分组件，`.` 丢弃，`..` 从组件栈弹出；
5. 按前缀与组件栈重拼，末尾补 0。

组件栈以 `CCDyn` 保存 `char *`，指向规范化缓冲内部；输出使用按需扩容的字节缓冲。设路径长度为 L：

| 操作 | 复杂度 |
|---|---|
| 规范化 | O(L) 时间，O(L) 辅助空间 |
| 目录提取 `mc_path_dirname` | O(L) |
| 路径比较 `mc_path_equal` | O(L) |

路径缓冲按需扩容，长度只受内存约束。

### 7.6 include 扫描：单遍字节状态机

`mc_scan_includes` 对文件内容执行一次从左到右的扫描，状态为：

| 状态 | 转移 |
|---|---|
| 普通 | 遇 `//` 进入行注释；遇 `/*` 进入块注释；遇 `"` 进入字符串；遇 `{`、`[` 深度加一；遇 `}`、`]` 深度减一；遇 `@` 读取指令名 |
| 行注释 | 遇换行回到普通 |
| 块注释 | 遇 `*/` 回到普通 |
| 字符串 | 遇 `"` 回到普通 |

扫描只向前推进，`/*`、`*/`、`//`、`"` 等双字节判断使用常数级前瞻。行号在换行时递增，列号按字节递增。设内容总字节数为 C：

| 操作 | 复杂度 |
|---|---|
| 状态机扫描 | O(C) 时间，O(1) 辅助空间 |
| 提取 include 边 | 每条边一次路径拷贝，总计 O(m·L) |

扫描器不回溯、不递归；每个字节最多访问一次。

### 7.7 合成挂载点：序号探测

VFS 合成路径采用 `/__mc:<序号>` 形式。装载前从序号 0 开始，用 `ccfiles_stat` 探测候选路径，遇到 `CC_FS_ERR_NOENT` 即选用该路径：

| 项 | 说明 |
|---|---|
| 路径构造 | `snprintf` 生成固定上界长度的名称 |
| 探测 | 每次 `ccfiles_stat` 一次 VFS 查询 |
| 期望开销 | 无冲突时首次命中，O(1) |
| 最坏开销 | 前 k 个名称已占用时为 O(k) |

每次 `mc_locate` 结束都会摘除本次建立的合成节点，下一次定位从空闲序号重新开始。

### 7.8 资源预算

阶段一从 `CCMakeMCContext.limits` 读取输入预算，在分配前检查：

| 预算 | 检查点 |
|---|---|
| `max_file_bytes` | 读取单个文件前 |
| `max_total_bytes` | 累加文件内容前 |
| `max_files` | 根文件与 include 文件追加前 |
| `max_include_edges` | include 扫描追加边前 |
| `max_include_depth` | include 目标追加前 |

超限时输出明确原因，例如 `单个文件超过大小限制`、`include 数量超过限制`、`include 深度超过限制`。阶段二的 token、数组、对象、`${find}` 预算见 [MC内容解析](MC内容解析.md)。

### 7.9 复杂度汇总

设 n 为文件数、m 为 include 边数、L 为路径长度、C 为内容总字节数：

| 操作 | 数据结构 / 算法 | 复杂度 |
|---|---|---|
| 文件追加 | `CCDyn` 尾部追加 | 摊还 O(1) |
| 文件查重 | 线性顺序查找 + 路径比较 | 单次 O(n·L) |
| 文件表访问 | 连续数组 + 下标 | O(1) |
| include 边访问 | 连续数组 + 区间下标 | O(1) |
| 图遍历 | 显式栈 DFS + 三状态标记 | O(n + m) 结构操作 |
| 循环检测 | 目标状态判断 | 每次边解析 O(1) |
| 解析顺序生成 | `order` 尾部追加 | 摊还 O(1)/文件 |
| 路径规范化 | 组件栈 + 动态缓冲 | O(L) |
| include 扫描 | 单遍字节状态机 | O(C) |
| 挂载点选择 | 序号探测 | 期望 O(1)，最坏 O(k) |

整体开销由 include 扫描、路径处理与查重共同构成：

```text
O(C + m·L + (n + m)·n·L + n + m)
```

文件表与边表均为连续数组，访问局部性稳定；查重按登记顺序推进，文件项与边项之间的引用全部使用下标。

---

## 8. 接口

```c
void mc_context_init(CCMakeMCContext *ctx);
void mc_context_clear(CCMakeMCContext *ctx);

CCBOOL mc_locate(
    const char *root_path,
    const char *work_dir,
    CCMakeMCContext *ctx
);

void mc_print_locations(CCMakeMCContext *ctx);
```

| 接口 | 行为 |
|---|---|
| `mc_context_init` | 清零上下文 |
| `mc_locate` | 完成检查、定位、内容读取与顺序生成；返回前释放 VFS |
| `mc_print_locations` | 按 `order[]` 输出文件列表，供 `-v` 使用 |
| `mc_context_clear` | 释放 VFS（若仍存在）、文件内容、路径、边表与数组 |

`parse_mc` 调用 `mc_locate`，在 `-v` 下输出定位结果，然后将上下文交给内容解析阶段。

---

## 9. 诊断

所有阶段一错误以统一前缀输出：

```text
配置文件定位失败：<文件>:<行>: <说明>
```

根文件级错误：

```text
配置文件定位失败：无法读取配置文件 "<路径>"：<说明>
```

include 级错误：

```text
配置文件定位失败：<包含者>:<行>: 无法导入 "<目标>"：<说明>
```

常见说明：

| 场景 | 说明 |
|---|---|
| 根路径为空 | `缺少 -c/--config` |
| 工作目录无效 | `工作目录不存在或不是目录` |
| 文件缺失 | `文件不存在` |
| 目标是目录 | `目标是目录，不是文件` |
| 读取失败 | `无法打开文件` / `无法读取` / `读取不完整` |
| 编码不支持 | `不支持 UTF-16 编码，请使用 ASCII/UTF-8` |
| 二进制内容 | `文件中包含 NUL 字节，疑似二进制文件` |
| 循环导入 | `检测到循环导入：<链>` |

定位采用首错终止：遇到第一个错误立即返回，不继续定位其余文件。

---

## 10. 资源生命周期

```text
mc_context_init
  → mc_locate
       ├─ 成功：files / includes / order 完整，VFS 已释放
       └─ 失败：文件表、边表与内容保留在上下文中，VFS 已释放
  → mc_context_clear
       ├─ 若 VFS 仍存在：摘除全部挂载点并销毁探针
       ├─ order（最后创建）→ stack
       ├─ includes（逆序）→ files（逆序）
       └─ work_dir（最早创建）
```

`root_path` 归一化结果与每条 include 的 `resolved` 都在创建它们的函数内释放；文件表保存的是自己复制的路径副本，不接管调用方指针。

阶段一结束后，`files[]` 中的内容快照与 VFS 无关。任何失败路径都经由统一清理出口归还 VFS 节点，不在全局虚拟树中遗留 `/__mc:*` 节点。

---

## 11. 与内容解析阶段的接口

内容解析阶段读取以下数据：

| 字段 | 用途 |
|---|---|
| `files[i].real_path` | 诊断与来源标识 |
| `files[i].content` / `size` | 已去 BOM 的完整内容 |
| `files[i].includes` | include 边及行号 |
| `order[]` | 解析顺序 |

阶段二按 `order[]` 逐文件解析，遇到 `@include` 语句时直接跳过：include 展开已由阶段一完成，顺序由后序序列表达。

---

## 12. 验证

| 套件 | 覆盖 |
|---|---|
| `MC` | 单文件、链式、菱形、重复 include、直接与间接循环、注释/字符串、非法指令、缺失与任意后缀、根错误、BOM/CRLF/NUL/UTF-16、`-C`、绝对路径、空格路径、Windows 大小写去重、连续定位、路径规范化、失败清理 |
| `PARSE` | include 文件数量、边数量、深度预算与 `${find}` 结果预算 |

测试 fixture 位于 `tests/data/mc/`，按用例分为单文件、链、菱形、循环、注释、错误指令、编码、工作目录、大小写等目录。编码类 fixture 以原始字节保存，`.gitattributes` 关闭文本换行转换。

---

## 13. 边界

- 第一阶段只识别 `@include` 指令，不解析键值、数组、对象与内建函数。
- include 路径是字面量，不支持变量展开、环境变量、`~` 与通配符。
- 文件内容在定位时形成快照；同一文件在阶段一与阶段二之间不会被重新读取。
- 路径按词法规范化，不解析软链接目标；同一文件经不同软链接拼写进入时按不同路径处理。
- Windows 下非 ASCII 路径由宿主 ANSI 代码页解释，`.mc` 文件中 include 路径的编码与宿主代码页保持一致。
- 阶段一不切换进程工作目录；`-C` 只参与根路径解析。
