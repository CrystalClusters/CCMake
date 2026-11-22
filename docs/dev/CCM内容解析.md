# CCM 内容解析

第二阶段读取第一阶段产出的文件表与解析顺序，把配置内容写入全局变量树。本文描述解析器结构、字符扫描、语法拆分、内建函数、栈方案与诊断。

文件是否属于配置由内容决定，扩展名不作为判定条件；`.ccm` 是推荐的文件名约定。

## 1. 模块与输入

| 模块 | 职责 |
|---|---|
| `src/ccm/ccm_scan_util.c` | 共享字符串边界与 `${...}` 花括号配对扫描 |
| `src/ccm/ccm_scan.c` | 第一阶段扫描 `@include`，提取路径字面量 |
| `src/ccm/ccm_locate.c` | 文件注册表、include 闭包、环检测、后序 `order[]` |
| `src/ccm/ccm_parse.c` | 第二阶段词法、语法解析、写全局变量树 |
| `src/ccvar.c` | 全局变量树：路径、对象红黑树、数组、删除 |
| `src/ccm/ccm_builtin.c` | `${ref}` / `${find}` 分派与序列化 |
| `src/ccm/ccm_find.c` | `${find}` 的 VFS 目录遍历与通配符匹配 |

| 项 | 内容 |
|---|---|
| 输入 | 第一阶段 `files[]` 的内容快照与 `order[]` 解析顺序 |
| 输出 | 全局变量树中的完整配置数据 |
| 依赖 | 全局变量树、CrystalStd VFS（`${find}`） |
| 阶段边界 | 内容解析不重新定位 `.ccm` 文件，`@include` 的路径解析由第一阶段完成 |

## 2. 解析总流程

```text
main
  → ccvar_global_init
  → parse_ccm
      → ccvar_clear
      → ccm_locate
      → ccm_parse
  → ccvar_global_deinit
```

`ccm_parse` 只使用 `files[]` 的内存快照与 `order[]`，不访问 `.ccm` 源文件；`${find}` 按自身参数访问目录。

### 2.1 文件顺序

`order[]` 是 include 闭包的后序序列：

- 被包含文件先于包含者；
- 多个 `@include` 按出现顺序；
- 重复 include 只出现一次；
- 根文件最后。

### 2.2 文件内顺序

单个文件按语句从上到下解析：

- 顶层字段按出现顺序；
- 对象成员按出现顺序；
- 数组元素按出现顺序追加；
- 同名键后写入者覆盖先写入者。

### 2.3 覆盖优先级

```text
更早的 include 文件 < 更晚的 include 文件 < 包含者文件
文件内：更早的语句 < 更晚的语句
```

该顺序由 `order[]` 与解析器的线性扫描共同确定。

## 3. 解析器结构

```c
typedef struct cc_make_token {
    CCMTokenType type;
    const char *text;   /* 指向文件内容，不复制 */
    CCUINT32     len;
    CCUINT32     line;
    CCUINT32     col;
} CCMToken;

typedef struct cc_make_lexer {
    const char *data;
    CCUINT64     size;
    CCUINT64     pos;
    CCUINT32     line;
    CCUINT32     col;
    const char *file;
} CCMLexer;

typedef struct cc_make_parser {
    CCMLexer            lex;
    CCMToken            tok;
    CCVarTree          *tree;
    CCMakeCCMContext   *ctx;
} CCMParser;
```

- `lex_next` 从 `lex->pos` 读取下一个 token；token 的 `text` 指向文件内容，`len` 是字节长度；
- `parser_advance` 推进一个 token；
- `parser_need` 校验当前 token 类型；
- `parser_expect` 校验后自动推进。

文件内容在第一阶段已复制为带结尾 0 的快照，并剥离 UTF-8 BOM、拒绝 NUL 与 UTF-16/UTF-32。

## 4. 词法扫描

词法分析是单字节推进的状态机，不按行切分，也不使用 `strtok`、正则或固定分隔符表。换行只用于行号统计、注释结束和空白跳过。

### 4.1 空白与注释

| 输入 | 处理 |
|---|---|
| 空格、`\t`、`\f`、`\v` | 跳过，列号递增 |
| `\n` / `\r` / `\r\n` | 更新行号，列号回到 1 |
| `//` | 跳到行尾，不消费换行 |
| `/* ... */` | 跳到 `*/`；未闭合报错 |
| `"` | 进入字符串扫描 |

块注释不嵌套。注释只作为 token 间空白处理，不产生 token。

### 4.2 字符串

- 起点是 `"`，终点是下一个 `"`；
- 中间内容原样作为 `CCM_TOK_STRING` 的 `text`/`len`；
- 不做反斜杠转义；
- 字符串内部允许换行，换行会更新行号；
- 未闭合字符串在起始行报错。

因此字段名、字符串值和 `@include` 路径都使用同一套双引号规则。

### 4.3 符号与数字

| 输入 | Token |
|---|---|
| `{` `}` `[` `]` `:` `;` `,` | 对应单字符符号 |
| `-` 或数字 | `CCM_TOK_NUMBER` |
| 字母、`_` | 标识符；`true` / `false` 映射布尔 token |
| 其他字符 | `无法识别的字符` |

数字扫描顺序：

1. 可选 `-`；
2. 至少一位十进制数字；
3. 可选 `.` 且后面至少一位数字；
4. 可选 `e`/`E`，可选正负号，至少一位指数数字。

整数使用 `strtoll` 并检查 `end` 与 `ERANGE`。浮点使用自带的十进制转换：

- 整数部分、小数部分、指数部分分别累积到 `long double`；
- 指数使用平方求幂计算 10 的幂，不调用 `strtod`；
- 结果转换为 `double` 后用 `isfinite` 检查有限性；
- 溢出、下溢到 0、超出 `double` 范围都报错；
- 转换不依赖进程 `LC_NUMERIC`，构建结果不受运行机器区域设置影响。

该转换只接受词法阶段已经验证的十进制格式，不处理 `nan`、`inf` 或十六进制浮点。

### 4.4 指令与内建函数

`@`：

- 读取 `[A-Za-z0-9_]+`；
- 名称等于 `include` 时产生 `CCM_TOK_AT_INCLUDE`；
- 其他指令在第二阶段报“未知的 @ 指令”；
- 第一阶段的扫描器只处理 `include`。

`${`：

- 从 `${` 后开始记录原始 token；
- 用花括号深度计数寻找配对的 `}`；
- 内部字符串按双引号跳过；
- 支持嵌套 `${...}`；
- token 文本不包含 `${` 和 `}`，交给内建函数分派。

### 4.5 阶段一的 include 扫描

第一阶段 `ccm_scan_includes` 与第二阶段词法分离：

- 单遍字节扫描；
- 状态为普通、行注释、块注释、字符串；
- 普通状态维护 `{`/`[` 的括号深度；
- 只在深度 0 识别 `@include`；
- 提取双引号路径、记录行号/列号、要求分号；
- 路径中的控制字符与空路径报错。

键值语法、未知 `@` 指令、括号配对等留给第二阶段。

### 4.6 共享扫描基元

`src/ccm/ccm_scan_util.c` 提供两个不依赖业务状态的边界函数：

| 函数 | 用途 |
|---|---|
| `ccm_scan_string_end` | 从起始双引号找到闭合双引号，检查 NUL |
| `ccm_scan_builtin_end` | 从 `${` 后的 `{` 找到配对 `}`，引号内不计数花括号 |

第二阶段 lexer 的字符串与 `${...}` 边界直接复用上述基元；`${find}` 参数展开与分词也复用基元，避免各自的引号规则漂移。

第一阶段的 `ccm_scan_includes` 仍保留带行号/列号的状态机，因为它同时处理注释、括号深度与 include 字面量提取；其字符串与花括号语义与共享基元保持一致。

## 5. 语法与拆分

```text
file        := statement*
statement   := include | field
include     := "@include" string ";"
field       := string ":" value ";"
value       := string | number | "true" | "false" | object | array | builtin
object      := "{" (string ":" value ";")* "}"
array       := "[" (value (","? value)*)? ","? "]"
```

### 5.1 顶层流程

`parser_parse_file`：

1. 初始化 lexer 和 parser；
2. 读取第一个 token；
3. 循环直到 EOF：
   - 遇到 `@include`：消费路径字符串与分号，跳过；
   - 否则调用 `parser_parse_top_field`；
4. 任意一步失败立即返回，错误带文件与行号。

`parser_parse_top_field` 负责字段名校验、路径构造、冒号/值/分号消费；路径只在 `Failed` 标签释放一次。函数末尾通过 `parser_expect` 消费分号并推进 token。

### 5.2 键名与路径

```text
key := [A-Za-z_][A-Za-z0-9_]*
```

- ASCII、大小写敏感；
- 首字符字母或 `_`，后续字母、数字或 `_`；
- 顶级字段与对象成员使用同一规则；
- 字段路径由 `parser_path_key` 拼接：`父路径 + "." + 键`；
- 数组元素路径由 `parser_path_index` 拼接：`父路径 + "[n]"`。

路径示例：

```text
toolchain
toolchain.c_compiler
defines[0]
targets[2].sources[1]
```

### 5.3 值分派

`parser_parse_value` 使用 `CCMValueFrame` 显式栈分派容器：

| Token | 行为 |
|---|---|
| 字符串 | `ccvar_set(..., CC_VAR_STRING, ...)`，长度使用 token 的 `len` |
| 数字 | 解析为 `CCINT64` 或 `double`，再 `ccvar_set` |
| `true` / `false` | 写入 `CC_VAR_BOOL` |
| `{` | 写入对象，压入 OBJECT 帧，消费 `}` 后弹出 |
| `[` | 写入数组，压入 ARRAY 帧，消费 `]` 后弹出 |
| `${...}` | 调用内建函数，结果按字符串写入 |
| 其他 | `值格式非法` |

嵌套对象/数组不再使用 C 调用栈递归；深度只作为可配置策略 `limits.max_value_depth`（默认 0 = 不限）。

### 5.4 对象

OBJECT 帧循环：

```text
while 当前 token != "}":
    若 after_value：消费分号并计数
    成员名必须是字符串并符合键名规则
    生成子路径
    若值是对象/数组：写入容器，压入子帧，父帧标记 after_value
    否则：写入标量，标记 after_value
```

顶层字段对象使用 `CC_VAR_WRITE_MERGE`；对象内部的对象成员使用 `CC_VAR_WRITE_REPLACE`。子路径所有权在压栈时转移给子帧，弹出/失败时统一释放。

### 5.5 数组

ARRAY 帧循环：

- 字符串、数字、布尔直接追加；
- `{` 追加对象节点并压入 OBJECT 子帧；
- `[` 追加数组节点并压入 ARRAY 子帧；
- 子帧路径由父路径 + `[index]` 生成，所有权随帧；
- 元素之间可用逗号或空白分隔；
- 允许尾逗号；
- 数组元素顺序就是 `ccvar_array_push` 的调用顺序。

### 5.6 空容器

- `{}` 产生空对象；
- `[]` 产生空数组；
- 空对象/空数组不写子节点，但类型与节点本身存在。

## 6. 对象合并

| 位置 | 模式 | 行为 |
|---|---|---|
| 顶层字段对象 | `CC_VAR_WRITE_MERGE` | 保留既有成员，覆盖出现的成员 |
| 对象内部的对象成员 | `CC_VAR_WRITE_REPLACE` | 旧子树整体替换 |
| 数组元素中的对象 | 新建对象 | 不参与外部合并 |

该组合实现规范中的浅合并：

```text
base:  "a": { "x": 1; "y": 2; "n": { "p": 3; }; };
later: "a": { "x": 9; "n": { "q": 4; }; };

结果:
  a.x = 9
  a.y = 2
  a.n = { q: 4 }
```

## 7. 内建函数

入口：

```c
ccm_builtin_eval(ctx, raw, len, tree, &out, &out_len, file, line);
```

`raw` 是 `${...}` 内部的内容，不包含 `${` 和 `}`。

分派：

1. 跳过前导空白；
2. 读取 `[A-Za-z0-9_]+` 函数名；
3. 精确匹配 `ref` 或 `find`；
4. 其他名称报“未知的内建函数”。

### 7.1 `${ref}`

```text
${ref <路径>}
```

算法：

1. 从函数名后取路径文本；
2. 去掉尾部空白；
3. 复制成 NUL 结尾字符串；
4. `ccvar_get(tree, path, &value)`；
5. 按类型序列化；
6. 返回新分配的字符串，解析器按 `CC_VAR_STRING` 写入当前字段。

序列化规则：

| 类型 | 形式 |
|---|---|
| string | 原字节 |
| int | 十进制 |
| float | 十进制浮点 |
| bool | `true` / `false` |
| array | 逐个元素序列化，单个空格连接 |
| object | 报错，不能直接字符串化 |

`${ref}` 查询的是**当前解析位置已经写入的树状态**，不会重新解析源文件。

### 7.2 `${find}`

```text
${find <目录...> -include <通配符...> [-exclude <通配符...>]}
```

算法分四步：

#### 1. 展开嵌套内建函数

`ccm_expand_builtins` 扫描参数：

- 遇到 `${` 时调用共享的 `ccm_scan_builtin_end` 配对花括号；
- 该基元识别双引号边界，引号内的 `{`、`}` 不参与外层配对；
- 双引号内的 `${...}` 同样会展开，引号本身保留；
- 递归调用 `ccm_builtin_eval`；
- 将结果拼入参数缓冲区；
- 其他字节原样复制。

因此 `${find ${ref src} ...}` 与 `${find ... -include "${ref pattern}"}` 都会先求值，后者展开后仍由 `find_tokenize` 按引号去掉外层引号。

#### 2. 参数分词

`find_tokenize` 逐字节扫描：

- 空白（空格、制表、CR、LF）分隔参数；
- 双引号包裹的内容作为一个参数，引号本身不进入结果；
- 引号边界由共享的 `ccm_scan_string_end` 定位；
- `-include` / `-exclude` 切换后续参数类别；
- 其余参数按模式归入目录、include、exclude 列表。

#### 3. VFS 遍历

每个目录：

1. 相对 `ctx->work_dir` 规范化；
2. 校验宿主目录；
3. 创建独立 `CCFiles` 探针；
4. 挂载到合成路径 `/__find:N`；
5. 调用 `find_scan_dir`。

`CCMFindScan` 聚合 `fs`、资源预算、通配符、结果表、诊断位置与目录栈；`find_scan_dir(scan, vpath)` 只接收这两个参数。

遍历拆成四层：

| 函数 | 职责 |
|---|---|
| `find_scan_dir` | 创建根栈项、调用遍历、统一释放栈 |
| `find_scan_loop` | 弹出目录，驱动子目录 DFS |
| `find_scan_entries` | 列目录并逐个处理条目 |
| `find_scan_entry` | 组装子路径，压子目录或匹配文件 |

`find_scan_entry` 使用本地 `cv`/`cr` 持有待转移的路径：压栈成功后所有权交给栈项；失败时通过 `FailedJoined` / `FailedPaths` 两级标签释放，标签只释放该阶段已经确定分配成功的资源。结果路径 `joined` 在写入结果表后立即释放。`find_scan_entries`、`find_scan_loop`、`find_scan_dir` 都在各自作用域内完成清理，不使用通用 `Done` 标签，也不依赖 `ccalloc(NULL, 0, NULL)` 的空操作行为。

`CCMFindStackItem` 在每次构造时先 `memset`，再成对赋值 `vpath` / `rel` / `depth`，最后才压栈：

```c
CCMFindStackItem child;
memset(&child, 0, sizeof(child));
child.vpath = cv;
child.rel = cr;
child.depth = dir->depth + 1;
cc_dyn_core_push(&scan->stack, sizeof(child), sizeof(child), &child);
```

两条路径都在 `cc_dyn_core_push` 调用前完成初始化；赋值语句之间有序，压栈读取的是完整结构体。`find_make_item` 和 `find_make_child_paths` 也先置空输出指针，再在两条路径都成功后同时发布，避免半初始化指针泄漏到调用方。

`find_scan_dir` 使用显式栈：

```c
typedef struct cc_make_find_stack_item {
    char *vpath;  /* VFS 虚拟路径 */
    char *rel;    /* 相对查找根目录的路径 */
} CCMFindStackItem;
```

流程：

1. 根目录入栈；
2. 弹出目录，`ccfiles_list` 列目录；
3. 文件按 `-include` 匹配，再由 `-exclude` 排除；
4. 匹配文件路径去重后加入结果；
5. 子目录压栈，继续 DFS；
6. 结果按字符串排序，单个空格连接；
7. 卸载合成挂载点，再删除还原后的内存节点。

通配符使用 `*` / `?`。

### 7.3 结果写入

`${ref}` 与 `${find}` 的结果都按字符串写入当前字段，不直接写数组或对象节点。

## 8. `-D` 覆盖

全部文件解析完成后，按命令行 `-D/--define` 顺序写入全局变量树：

- 键按路径段解释，例如 `toolchain.c=clang`；
- 每个路径段遵循键名规则；
- 值按字符串写入；
- 优先级高于所有配置文件。

## 9. 资源预算

`CCMakeCCMContext.limits` 是统一的输入预算，所有上限在分配前检查：

```c
typedef struct ccmake_ccm_limits_struct {
    CCUINT64 max_total_bytes;
    CCUINT64 max_file_bytes;

    CCUINT32 max_files;
    CCUINT32 max_include_edges;
    CCUINT32 max_include_depth;

    CCUINT32 max_token_bytes;
    CCUINT32 max_array_elements;
    CCUINT32 max_object_members;
    CCUINT32 max_value_depth;

    CCUINT32 max_find_args;
    CCUINT32 max_find_results;
    CCUINT32 max_find_depth;
} CCMakeCCMLimits;
```

默认值：

| 字段 | 默认值 | 检查位置 |
|---|---:|---|
| `max_total_bytes` | 0 | `ccm_load_file`，累加前检查；0 表示不限 |
| `max_file_bytes` | 0 | `ccm_load_file`，分配内容前检查；0 表示不限 |
| `max_files` | 0 | 根文件与 include 追加前；0 表示不限 |
| `max_include_edges` | 0 | `ccm_scan_includes` 追加边前；0 表示不限 |
| `max_include_depth` | 0 | include 目标追加前；0 表示不限 |
| `max_token_bytes` | 0 | `lex_next` 生成字符串/数字/内建 token 前；0 表示不限 |
| `max_array_elements` | 0 | 数组元素追加前；0 表示不限 |
| `max_object_members` | 0 | 对象成员解析前；0 表示不限 |
| `max_value_depth` | 0 | 值递归入口与数组元素入口；0 表示不限 |
| `max_find_args` | 0 | `${find}` 参数 token 追加前；0 表示不限 |
| `max_find_results` | 0 | 匹配文件加入结果前；0 表示不限 |
| `max_find_depth` | 0 | `${find}` 子目录压栈前；0 表示不限 |

0 表示动态资源路径；只有显式设置非 0 值时才启用对应策略。文件读取仍必须检查宿主可表示长度（`UINT64_MAX`、`SIZE_MAX - 1`），这与用户策略无关。

启用预算时返回可理解的诊断，例如：

```text
token 超过长度限制
数组元素数量超过限制
对象成员数量超过限制
include 数量超过限制
${find} 结果数量超过限制
```

这些检查位于内存分配之前，避免用“分配失败”代替明确的输入限制。

## 10. 栈与遍历方案

### 10.1 第一阶段：文件 DFS 显式栈

第一阶段使用显式栈，不用递归：

```c
typedef struct ccmake_ccm_frame_struct {
    CCUINT32 file;
    CCUINT32 next;  /* 下一条待处理 include 边 */
} CCMakeCCMFrame;
```

伪代码：

```text
root 装载 → VISITING → push(root frame)

while stack:
    frame = top
    if frame.next < file.include_count:
        edge = include[frame.next]
        frame.next++
        set_top(frame)
        resolve edge
        if target 已登记:
            target VISITING → 报循环
            否则只连边
        else:
            装载目标 → VISITING → push
        continue

    file → VISITED
    order.push(file)
    pop
```

`order[]` 在此过程中形成后序序列，根文件最后入队。

### 10.2 第二阶段：文件循环 + 值帧栈

第二阶段文件级没有栈：

```text
for i in order[]:
    parser_parse_file(files[order[i]])
```

文件内部的值解析使用 `CCDyn` 的 `CCMValueFrame` 栈：容器打开时压栈，闭合时弹栈；标量直接写入。深度只作为 `max_value_depth` 策略，默认 0 表示不限。帧路径在压栈时转移所有权，弹出或失败时释放。

### 10.3 `${find}`：显式目录栈

`find_scan_dir` 用 `CCDyn stack` 保存 `{vpath, rel}`，弹出目录、列目录、压入子目录。结果列表与栈分离，遍历完成后统一排序、连接。

### 10.4 删除：路径列表 + 逆序叶子删除

全局变量树删除子树时：

1. 先序收集子树路径；
2. 反转路径列表；
3. 从叶子到根删除；
4. 内部用 `CCVarCollectItem` 保存 `{node, path}`。

具体节点搬移与红黑树修正见 [全局变量树](全局变量树.md)。

## 11. 诊断与生命周期

统一前缀：

```text
配置文件解析失败：<文件>:<行>: <说明>
```

常见提示：

| 场景 | 说明 |
|---|---|
| 键名非法 | `键名非法（应匹配 [A-Za-z_][A-Za-z0-9_]*）` |
| 值格式非法 | `值格式非法` |
| 对象/数组未闭合 | `对象未闭合` / `数组未闭合` |
| 引用不存在 | `引用的字段不存在：<路径>` |
| 对象不能字符串化 | `对象不能作为字符串引用` |
| 内建函数非法 | `${ref} 缺少字段名` / `${find} 需要目录与 -include 通配符` |
| `-D` 键非法 | `-D 键名非法：<键>` |

解析采用首错终止。失败时 `parse_ccm` 清空全局树，避免后续流程使用半成品配置。

```text
main
  ├─ ccvar_global_init
  ├─ parse_ccm
  │    ├─ ccvar_clear
  │    ├─ ccm_locate
  │    └─ ccm_parse → 写入全局树
  └─ ccvar_global_deinit
```

创建与释放都在 `main` 的同一生命周期内成对出现；`parse_ccm` 使用已创建的空树，解析失败时清空本次写入的内容。成功路径直接进入 `Done`，失败标签逐级回落到 `FailedtoParseCmd` 后统一销毁。

### 11.1 资源所有权

每个资源在任意时刻有且只有一个明确所有者；所有权转移显式发生，释放责任归最终所有者，借用者不得释放或延长资源生命周期。

| 资源 | 创建者 | 释放者 |
|---|---|---|
| `ccmake_global_var` 的字符串、`defines` | `parse_cmd` 写入 | `main` 调用 `clear_global_var` |
| 全局 `CCVarTree` | `main` 的 `ccvar_global_init` | `main` 的 `ccvar_global_deinit` |
| 全局树中的配置内容 | `ccm_parse` 写入 | `parse_ccm` 失败时 `ccvar_clear`，整树由 `main` 销毁 |
| `CCMakeCCMContext`、文件表、边表、`order` | `parse_ccm` 内的 `ccm_context_init` / `ccm_locate` | 同一函数内的 `ccm_context_clear` |
| 单个字段字符串、对象/数组节点 | `ccvar_set` / `ccvar_array_push` 内部复制 | 树内部覆盖或删除时释放 |
| `${ref}` / `${find}` 输出字符串 | `ccm_builtin_eval` | 调用方解析器释放 |
| `${find}` 的 `vpath` / `rel` / 结果路径 | `find_scan_*` 内部 | 对应 `find_scan_*` 作用域内释放 |

`clear_global_var` 只清理 `ccmake_global_var` 自己持有的字段；`ccvar_global_deinit` 只销毁全局树。两者不再在同一次运行中互相重复清理。

## 12. 复杂度

设文件数为 n、include 边数为 m、文件内容总字节数为 C、路径长度为 L、目录项数为 e：

| 操作 | 方案 | 复杂度 |
|---|---|---|
| 第一阶段文件 DFS | 显式栈 + 三状态 | O(n + m) 结构操作 |
| 第二阶段文件遍历 | `order[]` 线性循环 | O(n) |
| 词法扫描 | 单字节状态机 | O(C) |
| 对象/数组解析 | `CCMValueFrame` 显式栈 | O(节点数)，深度由 `max_value_depth` 策略约束（默认 0） |
| 资源预算检查 | 计数器 + 上限比较 | 每项 O(1) |
| 浮点转换 | 十进制累积 + 平方求幂 | O(token 长度) |
| `ccvar_set` 路径写入 | 路径解析 + 对象红黑树 | O(路径段 × log 同层键数) |
| `${ref}` | 路径查询 + 序列化 | O(路径 + 输出长度) |
| `${find}` | VFS DFS + 通配符匹配 | O(e × 模式数) |
| 结果排序 | `qsort` | O(k log k)，k 为结果数 |
| `-D` 覆盖 | 逐项路径写入 | O(d × 路径段 × log 同层键数) |

## 13. 验证

| 套件 | 覆盖 |
|---|---|
| `PARSE` | 基本类型、嵌套对象、文件顺序、浅合并、`${ref}`、嵌套数组、数组对象、重复键、空容器、错误、`${find}`、内嵌 `${ref}`、注释、`-D`、128/129 层深度边界、5000 层显式值帧栈、CCM 动态追加边界、token/数组/对象预算、include 文件/边/深度预算、`${find}` 结果预算、浮点范围、引号内建函数、失败后复用 |
| `VARS` | 路径写入、覆盖、对象合并、数组、子树删除、键名规则、大量同层键、清空复用 |
| `CCM` | 第一阶段文件定位、任意后缀、顺序与清理 |
