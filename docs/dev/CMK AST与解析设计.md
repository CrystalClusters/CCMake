# CMK AST 与解析设计

本文冻结 M2 的 AST 数据模型、语法边界、解析策略、资源所有权与验收顺序。M2 的目标不是“尽快写出一个能跑的 Parser”，而是先让后续 M3 语义分析、M4 Runtime 和 M5 Lowering 依赖的结构稳定下来。

- 语言能力与内建函数见 [CMK 文件规范](CMK文件规范.md)；
- 模块划分与里程碑见 [CMK 实现细则](CMK实现细则.md)；
- Token 与词法边界见 [CMK 词法与语法](CMK词法与语法.md)；
- 代码风格见 [代码风格指南](代码风格指南.md)。

本文是 M2.0 的设计冻结稿。M2.0 代码、M2.1 到 M2.5 的实现必须按本文执行；实现中发现的偏差先回写本文，再改代码。

## 1. 阶段边界

```text
CMKSource
  源码缓冲区、文件路径、内容快照
        │
        ▼
CMKLexer
  源码 → Token；不感知 AST
        │
        ▼
CMKParser
  Token → AST；只判断语法并构建树
        │
        ▼
CMKAST
  完整语法结构、源码位置、AST 级字符串表
        │
        ▼
M3 Semantic Analysis
  名称解析、类型检查、作用域、目标闭包
```

### 1.1 Parser 的职责

Parser 只做三件事：

1. 判断 Token 序列是否符合已冻结的语法；
2. 构建 AST 节点、字符串表和源码位置；
3. 在失败时返回一条语法诊断，并且不发布半成品 AST。

### 1.2 Parser 的禁止事项

| 禁止 | 原因 |
|---|---|
| Lexer 不知道 AST 的存在 | 词法阶段只产生 Token，避免层次耦合 |
| Parser 不执行表达式、不求值 | 求值属于 M4 Configure；Parser 只构造语法树 |
| Parser 不访问 `.ccm` 配置树 | 配置读取 `@{...}` 只是 AST 叶子，解析/解析后的绑定由 M3/M4 完成 |
| Parser 不解析 `@import` 指向的模块内容 | 模块加载、循环检测、符号暴露由 M3/Program 负责 |
| Parser 不创建 Target 或 Build Plan | Target 的语义闭包和 IR 属于 M3/M5 |
| Parser 不调用任何内建函数 | `command`、`host_info` 等只是普通调用语法，由 M3 绑定内建签名 |
| Parser 不修改全局构建状态 | Parser 是纯函数式构建过程，输入 Token，输出 AST |
| Parser 不读写 `CMKLexer.diagnostic` | 双方各持有自己的诊断对象，避免重复释放与状态污染 |

### 1.3 输入输出

| 项 | 内容 |
|---|---|
| 输入 | `CMKLexer` 的当前 Token 流；`CMKLimits` 预算 |
| 输出 | 成功时发布 `CMKAST *`；失败时 `*out_ast == NULL` |
| 所有权 | Parser 借用 Lexer；AST 由 Parser 创建，成功后所有权转移给调用方 |
| 诊断 | `CMKParser` 拥有自己的 `CMKDiagnostic`；Lexer 诊断只读复制 |

## 2. 冻结决策摘要

| 编号 | 决策 | 结论 |
|---|---|---|
| D1 | AST 所有权 | `CMKAST` 是节点、节点数组、字符串表、文件路径的唯一所有者 |
| D2 | 节点组织 | 节点数组 + `CCUINT32` 节点下标；父/子/兄弟链接；字符串用 `CCUINT32` 字符串下标 |
| D3 | 语法边界 | 以本文 §4 的 EBNF 为准；赋值是语句，不进表达式；比较禁止链式；调用只允许裸标识符 |
| D4 | 表达式解析 | 递归下降负责声明/语句/结构，Pratt 负责表达式；比较层级带链式比较保护 |
| D5 | `{}` 歧义 | Lexer 不区分；Parser 通过上下文提供 `parser_parse_block`、`parser_parse_target_fields`、`parser_parse_list` 三个入口 |
| D6 | 字符串 Token | Parser 读取字符串 Token 时立即复制到 AST；不缓存 Token 序列 |
| D7 | 诊断 | Parser 拥有自己的诊断；首错终止；失败不发布 AST |
| D8 | 资源预算 | `CMKLimits` 统一控制 Token、AST 节点、语法深度、表达式深度、参数、列表元素与顶层声明数量 |
| D9 | 文件拆分 | 不写单一巨型 `cmk_parser.c`；按基础、表达式、语句、声明拆分 |
| D10 | `@include` | 设 `CMK_AST_INCLUDE_DECL`，Parser 只记录路径，不加载 `.ccm` |
| D11 | Action | 不设 `CMK_AST_ACTION`；动作构造器按普通 `CMK_AST_CALL` 解析，M3 再分类 |
| D12 | 配置引用 | `CMK_AST_CONFIG_REF` 保存原始路径字符串；Parser 校验路径语法，M3 解析段并绑定 |

## 3. AST 数据模型

### 3.1 所有权

`CMKAST` 是唯一所有者。AST 一旦发布，节点和字符串不再由 Parser 修改。

| 资源 | 所有者 | 释放方式 | 借用规则 |
|---|---|---|---|
| `CMKAST` 对象 | 调用方（Parser 成功后转移） | `cmk_ast_destroy` | Parser 在失败路径内部销毁 |
| 节点数组 `nodes` | `CMKAST` | `cmk_ast_destroy` | 节点 ID 是稳定引用；节点指针只在两次添加节点之间有效 |
| 字符串表 `strings` | `CMKAST` | `cmk_ast_destroy` | `CMKASTString.data` 在 AST 生命周期内稳定，节点只保存字符串 ID |
| 源码文件路径 `file` | `CMKAST` | `cmk_ast_destroy` | 只读借用给诊断/调试输出 |
| `CMKLexer` | 调用方 | `cmk_lexer_clear` | Parser 借用，不释放、不修改 |
| `CMKParser` 诊断 | `CMKParser` | `cmk_parser_clear` | 不把 Lexer 诊断指针保存到 Parser |
| 解析失败的临时 AST | `CMKParser` 内部的 `cmk_parser_parse` | 失败时立即 `cmk_ast_destroy` | 不返回给调用方 |

规则：

1. 节点之间只通过 `CMKASTNodeId` 建立树结构，父节点不单独释放子节点。
2. Parser 不把 `CMKToken.text`、`CMKToken.string` 或 `CMKLexer` 内部指针保存到 AST。
3. 标识符、字符串字面量、导入路径、配置路径等文本一律复制到 AST 字符串表。
4. AST 创建成功后才允许发布；发布后不再新增、删除或重排节点。
5. AST 创建失败不能发布不完整的树。
6. `cmk_ast_validate` 在发布前运行，验证失败视为解析失败。

### 3.2 节点 ID、字符串 ID 与源码位置

```c
typedef CCUINT32 CMKASTNodeId;
typedef CCUINT32 CMKASTStringId;

#define CMK_AST_NODE_NIL   ((CMKASTNodeId)0xFFFFFFFFu)
#define CMK_AST_STRING_NIL ((CMKASTStringId)0xFFFFFFFFu)

typedef struct cmk_source_span {
    CCUINT64 offset;   // 起始字节偏移
    CCUINT64 width;    // 覆盖的字节数
    CCUINT32 line;     // 起始行，1 基
    CCUINT32 col;      // 起始字节列，1 基
    CCUINT32 end_line; // 最后一个字节所在行，1 基
    CCUINT32 end_col;  // 最后一个字节所在列，1 基（包含）
} CMKSourceSpan;
```

- `offset` / `width` 覆盖完整语法结构，包含关键字、标点、括号等；
- `line` / `col` 是起点；`end_line` / `end_col` 是最后一个字节的位置（包含），不是 one-past-end；
- 复合节点的 span 从第一个 Token 起点到最后一个已消费 Token 的末尾；
- 空文件、空节点或合成 AST 测试允许全 0 span；
- `end_col` 用包含式定义，避免 `col + width` 在 `CCUINT32` 边界上溢出。

### 3.3 AST 字符串表

```c
typedef struct cmk_ast_string {
    char *data;     // AST 拥有，末尾补 0
    CCUINT32 len;   // 有效字节数，可包含 0 字节
} CMKASTString;
```

- `cmk_ast_add_string` 复制调用方字节并追加结尾 0，不接管调用方指针；
- 字符串长度使用 `CCUINT32`；超过表示上限时返回 `CMK_AST_STRING_NIL`；
- 允许 `len == 0`，`data` 指向空字符串缓冲区；
- 字符串内容在 AST 生命周期内稳定，节点保存 `CMKASTStringId`，不保存 `char *`。

### 3.4 节点结构

```c
typedef enum {
    CMK_AST_MODULE = 0,
    CMK_AST_INCLUDE_DECL,
    CMK_AST_IMPORT_DECL,
    CMK_AST_IMPORT_ITEM,
    CMK_AST_VAR_DECL,
    CMK_AST_FUNC_DECL,
    CMK_AST_PARAM,
    CMK_AST_TARGET_DECL,
    CMK_AST_TARGET_FIELD,
    CMK_AST_TYPE,

    CMK_AST_BLOCK,
    CMK_AST_EXPR_STMT,
    CMK_AST_IF,
    CMK_AST_WHILE,
    CMK_AST_FOR_IN,
    CMK_AST_RETURN,
    CMK_AST_BREAK,
    CMK_AST_CONTINUE,
    CMK_AST_ASSIGN,

    CMK_AST_LITERAL,
    CMK_AST_IDENT,
    CMK_AST_DISCARD,
    CMK_AST_UNARY,
    CMK_AST_BINARY,
    CMK_AST_CALL,
    CMK_AST_INDEX,
    CMK_AST_MEMBER,
    CMK_AST_LIST,
    CMK_AST_CONFIG_REF
} CMKASTKind;

typedef struct cmk_ast_node {
    CMKASTKind kind;
    CMKSourceSpan span;

    CMKASTNodeId parent;
    CMKASTNodeId first_child;
    CMKASTNodeId last_child;
    CMKASTNodeId next_sibling;

    union {
        struct {
            CMKASTStringId name;
            CCBOOL is_const;
        } var_decl;

        struct {
            CMKASTStringId name;
            CCUINT32 param_count;
        } func_decl;

        struct {
            CMKASTStringId name;
        } target_decl;

        struct {
            CMKTargetFieldKind field;
            CMKASTStringId name;
        } target_field;

        struct {
            CMKImportKind kind;
            CMKASTStringId module;
            CMKASTStringId alias;
            CCBOOL wildcard;
        } import_decl;

        struct {
            CMKASTStringId module;
        } include_decl;

        struct {
            CMKASTStringId name;
            CMKASTStringId alias;
        } import_item;

        struct {
            CMKASTStringId name;
        } param;

        struct {
            CMKTypeKind kind;
        } type;

        struct {
            CMKASTStringId name;
        } ident;

        struct {
            CMKASTStringId path;
        } config_ref;

        struct {
            CMKLiteralKind kind;
            union {
                CCINT64 integer;
                double floating;
                CCBOOL boolean;
                CMKASTStringId string;
            } value;
        } literal;

        struct {
            CMKUnaryOp op;
        } unary;

        struct {
            CMKBinaryOp op;
        } binary;

        struct {
            CMKAssignOp op;
        } assign;

        struct {
            CMKASTStringId callee;
        } call;

        struct {
            CMKASTStringId member;
        } member;

        struct {
            CCBOOL is_top_level;
        } block;

        struct {
            CCBOOL has_else;
            CCBOOL is_top_level;
        } if_stmt;

        struct {
            CMKASTStringId name;
        } for_in;

        struct {
            CCBOOL has_value;
        } return_stmt;
    } as;
} CMKASTNode;

typedef struct cmk_ast {
    char *file;      // AST 拥有，可为 NULL（匿名源码）
    CCDyn nodes;     // CMKASTNode[]
    CCDyn strings;   // CMKASTString[]
    CMKASTNodeId root;
} CMKAST;
```

实现中每个枚举末尾带 `*_COUNT` 哨兵，只用于范围校验，不会出现在 AST 节点中。

- 节点按值存放在 `nodes` 连续数组中；扩容会搬移节点内存，因此外部只保存节点 ID，不跨添加操作保存节点指针；
- `parent` 由 `cmk_ast_append_child` 设置；根模块的 `parent` 为 `CMK_AST_NODE_NIL`；
- 子节点按源码顺序通过 `first_child` / `next_sibling` 串联；
- 固定子节点顺序由 §3.5 规定；Parser 不按“看起来方便”的顺序挂接。

### 3.5 节点分类与子节点布局

#### 3.5.1 声明节点

| 节点 | 子节点 | 载荷 | 说明 |
|---|---|---|---|
| `CMK_AST_MODULE` | 顶层项，按源码顺序 | 无 | AST 根；子节点可为 include/import/var/func/target/top-if |
| `CMK_AST_INCLUDE_DECL` | 无 | `module` | `@include` 的 `.ccm` 路径；Parser 不加载 |
| `CMK_AST_IMPORT_DECL` | `CMK_AST_IMPORT_ITEM` 列表 | `kind`、`module`、`alias`、`wildcard` | `@import func ...` / `@import target ... as 别名` |
| `CMK_AST_IMPORT_ITEM` | 无 | `name`、`alias` | 单个导入名；`alias` 可为 NIL |
| `CMK_AST_VAR_DECL` | `[0]` 类型，`[1]` 初始化表达式 | `name`、`is_const` | 顶层、函数内、块内变量声明 |
| `CMK_AST_FUNC_DECL` | `[0]` 返回类型，`[1..param_count]` 参数，`[last]` 函数体 | `name`、`param_count` | 函数体必须是 `BLOCK` |
| `CMK_AST_PARAM` | `[0]` 类型 | `name` | 函数参数 |
| `CMK_AST_TARGET_DECL` | `CMK_AST_TARGET_FIELD` 列表 | `name` | Target 名保持静态 |
| `CMK_AST_TARGET_FIELD` | `[0]` 字段声明类型，`[1]` 初始化表达式 | `field`、`name` | 字段名未知时 `field = CMK_TARGET_FIELD_UNKNOWN` |
| `CMK_AST_TYPE` | 标量/void 为 0；`list<T>` 为 `[0] T` | `kind` | 类型节点，不复制类型文本 |

#### 3.5.2 语句节点

| 节点 | 子节点 | 载荷 | 说明 |
|---|---|---|---|
| `CMK_AST_BLOCK` | 块项列表（变量声明或普通语句；顶层分支块为目标声明） | `is_top_level` | 函数体、普通块、if/while/for 分支、顶层 if 分支块 |
| `CMK_AST_EXPR_STMT` | `[0]` 表达式 | 无 | 非赋值表达式语句 |
| `CMK_AST_IF` | `[0]` 条件，`[1]` then，`[2]` else（可选） | `has_else`、`is_top_level` | 顶层 if 与普通 if 共用节点；由 `is_top_level` 区分结构约束 |
| `CMK_AST_WHILE` | `[0]` 条件，`[1]` 循环体 | 无 | `while (expr) statement` |
| `CMK_AST_FOR_IN` | `[0]` 元素类型，`[1]` 可迭代表达式，`[2]` 循环体 | `name` | 循环变量类型与名字 |
| `CMK_AST_RETURN` | `[0]` 返回值（可选） | `has_value` | `return;` 无子节点 |
| `CMK_AST_BREAK` | 无 | 无 | 循环控制 |
| `CMK_AST_CONTINUE` | 无 | 无 | 循环控制 |
| `CMK_AST_ASSIGN` | `[0]` 目标，`[1]` 值 | `op` | 赋值语句的语法节点；目标只能是 IDENT/DISCARD |

#### 3.5.3 表达式节点

| 节点 | 子节点 | 载荷 | 说明 |
|---|---|---|---|
| `CMK_AST_LITERAL` | 无 | `kind` 与值 | 整数/浮点/字符串/布尔 |
| `CMK_AST_IDENT` | 无 | `name` | 标识符引用 |
| `CMK_AST_DISCARD` | 无 | 无 | `_`，只允许作赋值目标 |
| `CMK_AST_UNARY` | `[0]` 操作数 | `op` | `!`、`-`、`+` |
| `CMK_AST_BINARY` | `[0]` 左，`[1]` 右 | `op` | 算术、比较、相等、逻辑 |
| `CMK_AST_CALL` | 实参表达式列表 | `callee` | 只允许 `ident(...)` |
| `CMK_AST_INDEX` | `[0]` 基表达式，`[1]` 下标表达式 | 无 | `base[index]` |
| `CMK_AST_MEMBER` | `[0]` 基表达式 | `member` | `base.member`；用于 `别名.目标名` |
| `CMK_AST_LIST` | 元素表达式列表 | 无 | `{ ... }` 列表字面量 |
| `CMK_AST_CONFIG_REF` | 无 | `path` | `@{...}` 内部原始路径；Parser 校验语法，M3 解析段 |

`CMK_AST_ASSIGN` 属于语句节点，不是表达式节点；它不能出现在 `BINARY`、`VAR_DECL` 初始化、`CALL` 实参等表达式位置。AST 校验按“语句根”处理它。

#### 3.5.4 不设的节点

| 建议节点 | M2 决定 | 原因 |
|---|---|---|
| `CMK_AST_ACTION` | 不设 | `command`、`mkdir` 等是普通调用；是否形成动作由 M3 按上下文与内建签名判定，Parser 不绑定内建 |
| `CMK_AST_IMPORT_ALIAS` | 不设 | 目标模块别名是 `IMPORT_DECL.alias`，函数别名是 `IMPORT_ITEM.alias`；别名不是独立语法结构 |
| `CMK_AST_TARGET_BODY` | 不设 | Target 体是独立语法入口，字段直接挂在 `TARGET_DECL` 下，避免无意义包装 |
| `CMK_AST_CONFIG_SEG` | M2 不设 | 配置路径以原始字符串保存；M3 解析为段并绑定，避免 Parser 与 M3 重复维护路径结构 |

`CMK_AST_MODULE`、`CMK_AST_INCLUDE_DECL`、`CMK_AST_DISCARD` 是相对建议清单的补充，分别用于 AST 根、`@include` 声明和 `_` 丢弃目标。

### 3.6 AST 接口

```c
CMKAST *cmk_ast_create(const char *file);
void cmk_ast_destroy(CMKAST *ast);

CMKASTNodeId cmk_ast_add_node(
    CMKAST *ast, CMKASTKind kind, const CMKSourceSpan *span
);
CMKASTStringId cmk_ast_add_string(
    CMKAST *ast, const char *data, CCUINT32 len
);
CCBOOL cmk_ast_append_child(
    CMKAST *ast, CMKASTNodeId parent, CMKASTNodeId child
);

CMKASTNode *cmk_ast_node_mut(CMKAST *ast, CMKASTNodeId id);
const CMKASTNode *cmk_ast_node(const CMKAST *ast, CMKASTNodeId id);
const char *cmk_ast_string(
    const CMKAST *ast, CMKASTStringId id, CCUINT32 *out_len
);
const char *cmk_ast_file(const CMKAST *ast);
CMKASTNodeId cmk_ast_root(const CMKAST *ast);
CCUINT32 cmk_ast_node_count(const CMKAST *ast);
CCUINT32 cmk_ast_string_count(const CMKAST *ast);
const char *cmk_ast_kind_name(CMKASTKind kind);
CCBOOL cmk_ast_kind_is_expression(CMKASTKind kind);
CCBOOL cmk_ast_kind_is_statement(CMKASTKind kind);
CCBOOL cmk_ast_kind_is_declaration(CMKASTKind kind);

CCBOOL cmk_ast_validate(const CMKAST *ast, const char **reason);

typedef CCBOOL (*CMKASTVisitFn)(
    const CMKAST *ast, CMKASTNodeId id, void *user
);
CCBOOL cmk_ast_visit(
    const CMKAST *ast, CMKASTNodeId root,
    CMKASTVisitFn fn, void *user
);
```

约定：

- `cmk_ast_create` 创建根 `CMK_AST_MODULE` 节点；根节点 ID 通常为 0，但调用方应使用 `cmk_ast_root`；
- `cmk_ast_add_node` 返回 `CMK_AST_NODE_NIL` 表示失败；分配的是节点数组容量，不单独分配节点；
- `cmk_ast_add_string` 返回 `CMK_AST_STRING_NIL` 表示失败；
- `cmk_ast_append_child` 只接受尚未挂接的节点；子节点已有 parent、child 是根节点、`parent == child`、或 child 是 parent 的祖先时失败；
- `cmk_ast_node_mut` 供 Parser 构建期和 M2.0 手工测试使用；AST 发布后调用方只使用 `cmk_ast_node`；
- `cmk_ast_node` / `cmk_ast_string` 返回借用指针；节点指针在后续 `cmk_ast_add_node` 后失效，字符串数据指针在 AST 生命周期内稳定；
- `cmk_ast_visit` 先序遍历；visitor 返回 `CCFALSE` 时中断并返回 `CCFALSE`；
- `cmk_ast_validate` 只读，不改树；失败时 `*reason` 指向静态字符串。

### 3.7 AST 校验

`cmk_ast_validate` 在发布前检查：

1. 根节点存在、kind 为 `CMK_AST_MODULE`、`parent == NIL`；
2. 节点 ID、字符串 ID 在有效范围内；
3. 树无环、无重复挂接；从根可达节点数等于 `node_count`（检测孤儿节点）；
4. 每种 kind 的子节点数量与 §3.5 一致；
5. 子节点 kind 符合上下文：
   - `VAR_DECL[0]`、`PARAM[0]`、`TARGET_FIELD[0]`、`FOR_IN[0]` 必须是 `TYPE`；
   - `FUNC_DECL[0]` 是返回类型 `TYPE`，最后一个子节点是 `BLOCK`，中间是 `PARAM`；
   - `ASSIGN[0]` 是 `IDENT` 或 `DISCARD`；`ASSIGN` 只能作为语句节点，不能作为表达式子节点；
   - `IF` 的条件是表达式；`is_top_level == CCFALSE` 时分支只能是普通语句，`is_top_level == CCTRUE` 时分支只能是 `TARGET_DECL`、顶层 `IF` 或顶层 `BLOCK`；
   - `WHILE` / `FOR_IN` 的循环体只能是普通语句，不能是 target 或顶层结构；
   - `MODULE` 只允许顶层声明、导入、包含与顶层 if；出现在 module 的 `IF` 必须带 `is_top_level == CCTRUE`；
   - 普通 `BLOCK` 只允许块项中的变量声明与普通语句，拒绝 `TARGET_DECL` 和顶层 `IF` / 顶层 `BLOCK`；
   - 顶层 `BLOCK` 只允许 `TARGET_DECL` 与顶层 `IF`；
6. 类型节点 `list` 有且只有一个元素类型；标量/void 没有子节点；
7. 字符串引用非 NIL 且有效；`literal` 的字符串只在 `CMK_LITERAL_STRING` 时使用；
8. 节点 payload 与 kind 匹配。

校验失败的状态属于 Parser 内部错误，诊断前缀使用“AST 内部校验失败：”，不发布 AST。

### 3.8 AST 形态示例

`int count = 10;`

```text
(module
  (var-decl "count" const=0
    (type int)
    (literal int 10)))
```

`if (count > 0) { count += 1; }`

```text
(if has-else=0
  (binary `>`
    (ident "count")
    (literal int 0))
  (block
    (assign `+=`
      (ident "count")
      (literal int 1))))
```

`target app() { list<string> outputs = {"build/app"}; }`

```text
(target-decl "app"
  (target-field outputs "outputs"
    (type (list string))
    (list
      (literal string "build/app"))))
```

`@import target core, utils from "targets/common.cmk" as common;`

```text
(import-decl kind=target module="targets/common.cmk" alias="common"
  (import-item "core")
  (import-item "utils"))
```

## 4. 语法边界

### 4.1 完整 EBNF

以下 EBNF 与 [CMK 词法与语法](CMK词法与语法.md) 保持同步，是 M2 的语法依据：

```text
file            := { import_decl | include_decl | top_item } ;

import_decl     := "@import" ( func_import | target_import ) "from" string ";" ;
func_import     := "func" ( "*" | ident { "," ident } ) [ "as" ident ] ;
target_import   := "target" ident { "," ident } "as" ident ;
include_decl    := "@include" string ";" ;

top_item        := declaration | top_if ;
top_if          := "if" "(" expression ")" top_branch [ "else" top_branch ] ;
top_branch      := target_decl | top_if | "{" { target_decl | top_if } "}" ;

declaration     := variable_decl | function_decl | target_decl ;
variable_decl   := [ "const" ] type ident "=" expression ";" ;

type            := scalar_type | "list" "<" type ">" ;
scalar_type     := "string" | "int" | "bool" | "float"
                 | "action" | "target_ref" ;
return_type     := type | "void" ;

function_decl   := "func" ident "(" [ parameter { "," parameter } ] ")"
                   "->" return_type block ;
parameter       := type ident ;

target_decl     := "target" ident "(" ")" target_body ;
target_body     := "{" { target_field } "}" ;
target_field    := type ident "=" expression ";" ;

block           := "{" { block_item } "}" ;
block_item      := variable_decl | statement ;

statement       := block | if_statement | while_statement
                 | for_statement | "break" ";" | "continue" ";"
                 | return_statement | assignment_statement
                 | expression_statement ;

if_statement    := "if" "(" expression ")" statement
                   [ "else" statement ] ;
while_statement := "while" "(" expression ")" statement ;
for_statement   := "for" "(" type ident "in" expression ")" statement ;
return_statement := "return" [ expression ] ";" ;
assignment_statement := ( ident | discard ) ( "=" | "+=" ) expression ";" ;
expression_statement := expression ";" ;

expression      := logical_or ;
logical_or      := logical_and { "||" logical_and } ;
logical_and     := equality { "&&" equality } ;
equality        := relational { ( "==" | "!=" ) relational } ;
relational      := additive [ ( "<" | ">" | "<=" | ">=" ) additive ] ;
additive        := multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative  := unary { ( "*" | "/" | "%" ) unary } ;
unary           := ( "!" | "-" | "+" ) unary | postfix ;
postfix         := primary { index_suffix | member_suffix } ;
index_suffix    := "[" expression "]" ;
member_suffix   := "." ident ;
primary         := int | float | string | "true" | "false"
                 | config_ref | ident [ call_suffix ]
                 | "(" expression ")" | list_literal ;
call_suffix     := "(" [ expression { "," expression } ] ")" ;
list_literal    := "{" [ expression { "," expression } [ "," ] ] "}" ;
config_ref      := "@{" config_path "}" ;
config_path     := key { "." key | "[" int "]" } ;
discard         := "_" ;
```

说明：

- `top_if` 是顶层专用入口，分支只允许 `target_decl` 或嵌套 `top_if`；普通 `if_statement` 用于函数/块内；
- `target_body` 是独立入口，不是普通 `block`；
- 变量声明是 `block_item`，不是 `statement`：`if (cond) int x = 1;` 语法错误，需要写成 `if (cond) { int x = 1; }`；
- `target_field` 的值是任意表达式，由 M3 检查是否为 `list<T>`；Parser 不把字段名限制为四个已知字段，未知字段保留 `CMK_TARGET_FIELD_UNKNOWN` 供 M3 报错；
- `@include` 与 `@import` 都只记录路径，不加载文件；
- `config_ref` 的 `config_path` 由 Parser 校验字符级语法，AST 只保存原始路径字符串；
- 列表字面量允许尾逗号；函数调用实参不允许尾逗号（第一版）。

### 4.2 相对上一版语法的冻结变化

| 变化 | 旧规则 | 新规则 | 原因 |
|---|---|---|---|
| 赋值 | 表达式级，右结合 | 语句级 `assignment_statement` | Pratt 更简单；禁止 `a = b = c` 和条件内赋值 |
| 比较 | 可链式 | 只能出现一个关系运算符 | 禁止 `a < b < c`，避免语义歧义 |
| 一元 | `!`、`-` | `!`、`-`、`+` | 与 C 风格一致；一元 `+` 仅用于数值 |
| 调用 | 任意 postfix 可调用 | 只有裸标识符可调用 | 无函数指针、无闭包；`（f)()` 与 `f()()` 语法拒绝 |
| 顶层 if | 任意语句 | 只允许 target/top_if | 顶层 if 只用于选择目标声明 |
| `@include` | 只有声明语义 | 增加 `CMK_AST_INCLUDE_DECL` | M3/Program 需要加载 `.ccm` |
| `{}` | 通用 `parse_braces` 风险 | 三个独立入口 | 消除代码块与列表字面量的歧义 |

### 4.3 优先级与结合性

从高到低：

| 优先级 | 运算符 | 结合性 | 说明 |
|---|---|---|---|
| 1 | 调用 `()`、索引 `[]`、成员 `.` | 左 | 调用只允许裸标识符；index/member 可叠加 |
| 2 | 一元 `!` `-` `+` | 右 | `+` 只对数值合法 |
| 3 | `*` `/` `%` | 左 | |
| 4 | `+` `-` | 左 | `+` 的字符串/列表分派由 M3 决定 |
| 5 | `<` `>` `<=` `>=` | 非链式 | 每个关系表达式最多一个关系运算符 |
| 6 | `==` `!=` | 左 | `a == b == c` 仍按左结合解析 |
| 7 | `&&` | 左 | 短路求值由 M4 负责 |
| 8 | `\|\|` | 左 | 短路求值由 M4 负责 |
| — | `=` `+=` | 语句级 | 不进入表达式优先级表 |

Pratt 绑定力（`lbp`）：

| 运算符 | `lbp` |
|---|---:|
| `\|\|` | 10 |
| `&&` | 20 |
| `==` `!=` | 30 |
| `<` `>` `<=` `>=` | 40 |
| `+` `-` | 50 |
| `*` `/` `%` | 60 |

左结合运算的右操作数使用 `lbp + 1`。关系层级使用一个局部 `relational_seen` 标记：同一层已经解析过一个关系运算符后，再遇到关系运算符直接报“不支持链式比较”。嵌套括号或嵌套表达式使用新的 `relational_seen`。

示例：

```text
a + b * c        → a + (b * c)
a * b + c        → (a * b) + c
a - b - c        → (a - b) - c
a < b < c        → 语法错误：不支持链式比较
a == b == c      → (a == b) == c（允许，类型检查由 M3 负责）
a == b < c       → a == (b < c)（允许）
!-a              → !(-a)
-a * b           → (-a) * b
```

补充：C 语法本身并不禁止 `a < b < c`，它会按左结合解析为 `(a < b) < c`，结果通常没有意义。本语言在语法层直接拒绝关系链，是比 C 更严格的选择；`==` / `!=` 仍按 C 风格左结合。

### 4.4 赋值语句

```c
int x = 1;       // 变量声明，不是赋值语句
x = 2;           // 赋值语句
x += 3;          // 赋值语句
_ = f();         // 丢弃返回值
```

- 赋值只允许出现在语句位置；
- 左值只能是标识符或 `_`；
- 不允许 `a = b = c`、`if (x = 1)`、`f(x = 1)`；
- AST 为 `CMK_AST_ASSIGN`，`[0]` 是 `CMK_AST_IDENT` 或 `CMK_AST_DISCARD`，`[1]` 是值表达式；
- 变量声明中的初始化 `=` 仍由 `CMK_AST_VAR_DECL` 表达，不复用赋值节点。

为什么选择语句级：

- Pratt 只处理真正的表达式，不需要定义“赋值表达式的类型/结果值”；
- 消除 C 风格 `if (x = 1)` 的笔误类别，条件位置只能出现表达式；
- `a = b = c` 这类链式赋值没有明确收益，拆成两条语句更清晰；
- `_ = f()`、`args += item` 等现有用法仍然是语句，语法和语义不变；
- 代价是用户不能写 `while ((x = next()) != 0)` 这类 C 惯用法；本语言 Configure 阶段没有这类 I/O，循环状态更新写在循环体内即可；
- Parser 需要一次单 Token 前瞻来区分赋值语句与表达式语句，这一成本已在 §5.3 处理。

### 4.5 `{}` 的上下文规则

Lexer 只产生 `LBRACE` / `RBRACE`，不区分用途。Parser 提供三个独立入口：

```c
static CMKASTNodeId parser_parse_block(CMKParser *parser, ...);
static CCBOOL parser_parse_target_fields(
    CMKParser *parser, CMKASTNodeId target_id
);
static CMKASTNodeId parser_parse_list(CMKParser *parser, ...);
```

| 入口 | 出现位置 | 内部语法 | 结果 |
|---|---|---|---|
| `parser_parse_block` | 函数体、普通语句块、if/while/for 分支 | `{ statement* }` | 返回 `BLOCK` 节点 ID |
| `parser_parse_target_fields` | `target name() { ... }` | `{ target_field* }` | 字段直接挂到传入的 `TARGET_DECL`，返回成功/失败 |
| `parser_parse_list` | 表达式位置 | `{ [expr {"," expr} [","]] }` | 返回 `LIST` 节点 ID |

- 不存在通用 `parser_parse_braces`；
- Parser 不通过回溯区分 `{}`；
- 空 `{}` 在三个入口中分别产生空块、空 target 体、空列表；
- `if (x) { ... }` 的 `{` 由语句入口决定，`list<string> x = { ... }` 的 `{` 由表达式入口决定。

### 4.6 调用、索引、成员

```c
f()                  // CALL，callee = "f"
f(a, b)              // CALL
f()[0]               // INDEX(CALL(f), 0)
f().name             // MEMBER(CALL(f), "name")，M3 判断是否合法
common.core          // MEMBER(IDENT("common"), "core")
files[0]             // INDEX(IDENT("files"), 0)
(f)()                // 语法错误：调用只允许裸标识符
f()()                // 语法错误：第二个 () 没有语法入口
```

- 不支持函数指针、匿名函数、闭包、动态调用；
- 成员访问是静态名字，成员名复制到 AST；
- 目标引用 `别名.目标名` 与普通成员访问共用 `CMK_AST_MEMBER`，由 M3 判断上下文是否合法。

### 4.7 `@include` 与 `@import`

- Parser 只记录路径字符串和导入项，不打开文件；
- `@include` → `CMK_AST_INCLUDE_DECL`，`module` 为路径；
- `@import func ...` → `CMK_AST_IMPORT_DECL(kind=FUNC)`，`module` 为路径，`wildcard` 表示 `*`；
- `@import target ... as 别名` → `CMK_AST_IMPORT_DECL(kind=TARGET)`，`alias` 必填；
- 函数导入的 `as` 只能用于单个函数名；批量导入带 `as` 由 Parser 报语法错误；
- 目标导入的 `as` 是模块别名，不是单个目标重命名；
- Parser 不检查目标是否存在、不检查模块循环、不合并符号；这些属于 M3/Program。

### 4.8 配置引用 `@{...}`

- Lexer 已保证花括号内部只含标识符字符、`.`、`[`、`]`；
- Parser 校验 `config_path` 形状：至少一个 key，`.` 后必须是 key，`[]` 内必须是十进制数字；
- AST 保存花括号内部原始字节（`CMK_AST_CONFIG_REF.path`），不拆段；
- M3 负责按 `.`/`[]` 拆段并查询配置树；
- Parser 不访问 `.ccm`，缺失字段属于 Configure 阶段错误。

### 4.9 顶层 if

- 顶层 `if` 使用 `top_if` 语法，分支只允许 `target_decl` 或嵌套 `top_if`；
- 函数/块内的 `if` 使用普通 `if_statement`，分支是任意语句；
- 顶层 `if` 的 AST 仍是 `CMK_AST_IF`，由 `is_top_level == CCTRUE` 标记；顶层分支的 `BLOCK` 同样标记 `is_top_level`，AST 校验据此拒绝普通上下文中的 target/顶层结构；
- 顶层 `if` 条件中的函数调用只构建 AST，不在 Parser 阶段执行。

允许的场景是“按 Configure 条件选择目标”：

```c
if (@{platform} == "windows") {
    target app() {
        list<string> outputs = {"build/app.exe"};
        list<action> actions = {
            mkdir("build"),
            command(@{toolchain.c}, {@{cflags}, "src/main.c", "-o", "build/app.exe"})
        };
    }
} else {
    target app() {
        list<string> outputs = {"build/app"};
        list<action> actions = {
            mkdir("build"),
            command(@{toolchain.c}, {@{cflags}, "src/main.c", "-o", "build/app"})
        };
    }
}
```

不允许的场景是“条件化模块符号”：

```c
if (@{debug}) {
    func log(string msg) -> void {
        info("%s", msg);
    }
}
```

条件化函数/变量声明会让模块符号表变成条件相关：导入该模块的其他文件可能看到不同的函数集合，M3 必须先求值配置条件才能做名称解析；模块级变量初始化顺序、函数可见性、目标字段求值环境都会随之不稳定。需要条件行为时，把分支放进无条件声明的函数体：

```c
func log(string msg) -> void {
    if (@{debug}) {
        info("%s", msg);
    }
}
```

条件化全局常量同样禁止；需要平台相关值时，使用无条件函数或 `host_info` 查询，在函数/目标字段内选择。目标本身是 Configure 阶段选择的构建单元，因此顶层 if 只用于选择 target 是安全的。

### 4.10 不设 Action 节点

`command`、`mkdir`、`copy`、`move`、`remove` 在语法上只是普通调用。Parser 不根据名字建立 `CMK_AST_ACTION`，也不检查参数数量。M3 在目标 `actions` 字段的上下文中绑定内建签名，M5 再生成动作 IR。

这样做的原因：

- Parser 不依赖内建注册表；
- 内建函数与用户函数保持同一调用语法；
- 未来新增动作构造器不需要修改 Parser。

## 5. Parser 架构

### 5.1 递归下降 + Pratt

| 部分 | 方式 |
|---|---|
| 模块、导入、声明、语句、块、target 字段 | 递归下降，每个产生式一个入口 |
| 表达式 | Pratt，按 §4.3 绑定力循环 |
| 关系比较 | Pratt 循环内使用 `relational_seen` 禁止链式 |
| 赋值 | 语句级入口，不进入 Pratt |

不使用：

- 回溯解析；
- 通用 `parse_braces`；
- 多错误恢复；
- 在 Parser 阶段求值、折叠常量、解析配置或加载模块。

### 5.2 Parser 接口

```c
typedef struct cmk_parser {
    CMKLexer *lexer;             // 借用，不释放
    CMKAST *ast;                 // 解析期间借用；发布/失败后置空
    CMKToken current;            // 当前 Token
    CMKToken previous;           // 最近消费的 Token，用于 span
    CMKToken lookahead;          // 单 Token 前瞻
    CCBOOL has_lookahead;
    CMKDiagnostic diagnostic;    // Parser 拥有
    CMKLimits limits;            // 值拷贝
    CCUINT32 token_count;
    CCUINT32 syntax_depth;
    CCUINT32 expression_depth;
    CCUINT32 declaration_count;
    CCBOOL failed;
    CCBOOL used;                 // 已调用过 parse；clear + init 后才能复用
} CMKParser;

CCBOOL cmk_parser_init(
    CMKParser *parser, CMKLexer *lexer, const CMKLimits *limits
);
void cmk_parser_clear(CMKParser *parser);

CCBOOL cmk_parser_parse(CMKParser *parser, CMKAST **out_ast);
const CMKDiagnostic *cmk_parser_diagnostic(const CMKParser *parser);
```

契约：

- `cmk_parser_init` 要求 `parser`、`lexer` 非空；`limits == NULL` 时使用默认预算；
- init 读取第一个 Token；Lexer 已在当前流位置；常规做法是先用 `cmk_lexer_init_source` 初始化 Lexer；
- init 失败返回 `CCFALSE`；Parser 进入可预测失败状态，仍可 `cmk_parser_clear`；
- `cmk_parser_parse` 进入时先把 `*out_ast` 置 NULL；
- 只有成功解析并通过 `cmk_ast_validate` 才发布 AST；
- 失败路径由 Parser 销毁本次创建的 AST；
- `cmk_parser_parse` 只允许调用一次；成功或失败后状态不再复用；
- Parser 不拥有 Lexer；调用方先清理 Parser，再清理 Lexer；
- Parser 不修改 Lexer 诊断；遇到词法错误时复制其消息与位置到自己的诊断。

### 5.3 Token 缓冲与字符串生命周期

Parser 保存 `current`、`previous` 和最多一个 `lookahead` Token。Lexer 的字符串解码缓冲只在下一次 `cmk_lexer_next` 前有效，因此：

1. Parser 读到字符串 Token 后，立即把 `string`/`string_len` 复制到 AST 字符串表；
2. 只有赋值语句判定需要单 Token 前瞻：当前 Token 是 `IDENT` 或 `_` 时才调用 `parser_peek`；
3. `parser_peek` 的前置条件是 `current.type != CMK_TOK_STRING`；违反时属于 Parser 内部错误；
4. `parser_advance` 先把 `current` 记为 `previous`，再取下一个 Token；`previous` 只使用 `offset`/`width`/`line`/`col` 计算 span，不再使用其 `text`/`string`；
5. Parser 不在 Token 结构中长期保存字符串指针，AST 只保存字符串 ID。

这样既满足 M2 需要的单 Token 前瞻，也避免 `current` 的字符串缓冲被 `lookahead` 覆盖。

### 5.4 入口函数拆分

```text
parser_parse_module
  ├─ parser_parse_import
  ├─ parser_parse_include
  ├─ parser_parse_top_item
  │    ├─ parser_parse_var_decl
  │    ├─ parser_parse_func_decl
  │    ├─ parser_parse_target_decl
  │    └─ parser_parse_top_if
  └─ parser_parse_top_branch

parser_parse_block
  └─ parser_parse_block_item
       ├─ parser_parse_var_decl
       └─ parser_parse_statement
            ├─ parser_parse_block
            ├─ parser_parse_if
            ├─ parser_parse_while
            ├─ parser_parse_for
            ├─ parser_parse_return
            ├─ parser_parse_break / continue
            ├─ parser_parse_assignment
            └─ parser_parse_expression_statement

parser_parse_target_decl
  └─ parser_parse_target_fields（独立 target 体入口）

parser_parse_expression
  ├─ parser_parse_binary(min_bp)
  ├─ parser_parse_unary
  ├─ parser_parse_postfix
  ├─ parser_parse_primary
  ├─ parser_parse_call_suffix
  ├─ parser_parse_list
  └─ parser_parse_config_ref
```

每个入口只消费自己的语法结构；失败返回 `CMK_AST_NODE_NIL` 并设置 `parser->failed`。节点 span 由入口记录起点、消费完成后回填。

### 5.5 源码位置

- Parser 在入口保存当前 Token 的 `offset`/`line`/`col`；
- 每次 `parser_advance` 更新 `previous`；
- 节点完成时用“起点 Token + `previous`”生成 `CMKSourceSpan`：
  - `offset = start.offset`；
  - `width = previous.offset + previous.width - start.offset`；
  - `line/col = start.line/col`；
  - `end_line = previous.line`；
  - `end_col = previous.col + previous.width - 1`（包含式）；
- 空节点（如空列表）使用当前 Token 起点作为 span；
- Root module 的 span 从第一个 Token 到最后一个非 EOF Token；空文件使用 EOF 位置，`width = 0`。

### 5.6 诊断与事务

```text
cmk_parser_parse
  ├─ *out_ast = NULL
  ├─ ast = cmk_ast_create(lexer->file)
  ├─ parse module
  │    ├─ 成功：cmk_ast_validate
  │    └─ 失败：cmk_ast_destroy(ast)，返回 CCFALSE
  └─ 成功：*out_ast = ast
```

- 首错终止；错误后不继续读取 Token；
- Lexer 错误复制为 Parser 诊断，消息前缀“词法错误：”；Lexer 诊断消息为空时使用固定文本“词法错误”；
- 语法错误包含期望 Token 与实际 Token；
- 预算错误使用 §6 的固定消息；
- AST 校验失败使用“AST 内部校验失败：<原因>”；
- `cmk_parser_clear` 只释放 Parser 自己的诊断并清空状态，不释放已发布 AST，也不释放 Lexer。

### 5.7 字面量转换

Parser 只把字面量 Token 转成 AST 字面量值，不做表达式求值：

| Token | AST 值 | 规则 |
|---|---|---|
| `INT` | `CMK_LITERAL_INT` / `CCINT64` | 十进制转 `CCINT64`，检查溢出与非法尾随字符 |
| `FLOAT` | `CMK_LITERAL_FLOAT` / `double` | 十进制转换，结果必须有限；拒绝溢出与下溢到 0 |
| `STRING` | `CMK_LITERAL_STRING` / 字符串 ID | 使用 Lexer 解码结果，立即复制到 AST 字符串表 |
| `true` / `false` | `CMK_LITERAL_BOOL` / `CCBOOL` | 直接写入 |

- 浮点转换不依赖进程区域设置，沿用 `.ccm` 解析器的十进制累积方案；
- 负号通常是 `CMK_AST_UNARY`，不并入整数字面量；
- 正整数字面量范围是 `0..CCINT64_MAX`；
- `-9223372036854775808` 是唯一例外：Parser 在一元负号上下文中识别该幅值，直接构造值为 `CCINT64_MIN` 的 `CMK_AST_LITERAL`，不先转换为超范围正数，也不产生有符号溢出；
- 其他绝对值超过 `CCINT64_MAX` 的负数字面量（如 `-9223372036854775809`）报“整数超出范围”；
- 字面量转换失败属于语法层错误，Parser 报位置明确的诊断。

## 6. 资源预算 `CMKLimits`

### 6.1 结构

```c
typedef struct cmk_limits {
    CCUINT32 max_tokens;
    CCUINT32 max_ast_nodes;
    CCUINT32 max_syntax_depth;
    CCUINT32 max_expression_depth;
    CCUINT32 max_function_params;
    CCUINT32 max_list_elements;
    CCUINT32 max_declarations;
} CMKLimits;

void cmk_limits_init(CMKLimits *limits);
```

- `0` 表示不限制，仅受内存与宿主资源约束；
- `cmk_limits_init` 写入默认值；
- `cmk_parser_init` 传入 NULL 时使用默认值，否则值拷贝调用方预算；
- Parser 不在各函数内硬编码数字，所有检查统一读 `parser->limits`。

### 6.2 默认值与检查点

| 字段 | 默认值 | 检查点 | 超限消息 |
|---|---:|---|---|
| `max_tokens` | 0（不限制） | Parser 每次从 Lexer 取 Token 前 | `Token 数量超过限制` |
| `max_ast_nodes` | 0（不限制） | Parser 创建 AST 节点前（含根节点） | `AST 节点数量超过限制` |
| `max_syntax_depth` | 1024 | 进入 block/if/while/for/function/target/type 嵌套前 | `语法嵌套深度超过限制` |
| `max_expression_depth` | 512 | 进入表达式递归、括号、一元、列表字面量前 | `表达式嵌套深度超过限制` |
| `max_function_params` | 0（不限制） | 函数参数追加前 | `函数参数数量超过限制` |
| `max_list_elements` | 0（不限制） | 列表元素追加前 | `列表元素数量超过限制` |
| `max_declarations` | 0（不限制） | 模块顶层项追加前（import/include/decl/top-if） | `模块顶层声明数量超过限制` |

### 6.3 与“无语言层配额”的关系

[CMK 实现细则](CMK实现细则.md) §10 规定 CMK 不设置语言层的 token、AST 节点、列表长度等配额。M2 的 `CMKLimits` 是 **Parser 的实现资源预算**，不是语言语义限制：

- 计数类预算（Token、AST 节点、函数参数、列表元素、顶层声明）默认全部为 0，即只受内存与 `CCUINT32`/`CCDyn` 表示边界约束；分配失败由 Parser 转换成明确诊断；
- 深度类预算（语法嵌套、表达式递归）默认开启，因为递归下降直接消耗 C 调用栈，栈溢出不是可移植、可恢复的错误，不能靠内存分配失败兜底；
- 深度默认值按 Windows 默认 1 MiB 栈保守取值；如果 M2.5 在 Windows/Linux/Android 完成压测，并确认目标平台统一使用更大栈，可以提高默认值；
- 所有字段都可以显式设为 0 关闭；正常项目的 AST 结构不依赖这些数字，M3/M4 不读取预算作为语义信息。

如果后续决定完全取消默认计数预算，只需修改 `cmk_limits_init`，Parser 代码不变。

### 6.4 其他边界

- AST 节点/字符串 ID 使用 `CCUINT32`；超过表示上限返回失败，不静默回绕；
- Token 数量使用 `CCUINT32`；Parser 在递增前检查；
- 深度计数器使用 `CCUINT32`；Parser 在递增前检查；
- 单个字符串长度由 `cmk_ast_add_string` 检查 `CCUINT32` 上限；
- 预算检查发生在内存分配和节点挂接之前。

## 7. 文件与接口布局

```text
src/cmk/
  cmk_ast.c/h            AST 节点、字符串表、span、校验、visitor
  cmk_limits.c/h         CMKLimits 与默认值
  cmk_parser.h           Parser 公共接口、CMKParser 结构
  cmk_parser_internal.h  Parser 内部辅助函数、Token/span/错误/预算
  cmk_parser.c           init/clear/parse、Token 缓冲、诊断、事务
  cmk_parser_expr.c      Pratt、primary、postfix、list、config ref
  cmk_parser_stmt.c      语句与代码块
  cmk_parser_decl.c      模块、导入、函数、target、target 字段、顶层 if
```

测试：

```text
tests/cmk/
  test_cmk_ast.c            AST 模型、所有权、校验、visitor
  test_cmk_parser_base.c    Token 缓冲、空文件、EOF、诊断、预算
  test_cmk_parser_expr.c    表达式优先级、结合性、调用/索引/成员、列表
  test_cmk_parser_stmt.c    变量声明、赋值、块、if/while/for/return
  test_cmk_parser_decl.c    函数、target、导入/包含、顶层 if、完整模块
  test_cmk_parser_stress.c  大型源码、深嵌套、限制、确定性随机输入
  cmk_ast_test_util.c/h     测试侧 S-expression 序列化与 AST 构造辅助
```

构建脚本已经递归收集 `src/**/*.c` 和 `tests/cmk/*.c`；新增文件不需要改构建脚本，只需在 `tests/test.c` 注册新套件。

## 8. M2 子阶段与验收

| 子阶段 | 工作 | 验收 |
|---|---|---|
| M2.0 | AST 数据模型、字符串表、span、所有权、校验、visitor、`CMKLimits` | 手工创建/遍历/销毁 AST；节点/字符串/文件所有权正确；校验能发现孤儿、环、子节点数量错误 |
| M2.1 | Parser 基础设施：Token 缓冲、advance/peek/expect、诊断、预算、事务外壳 | 空文件、EOF、非法 Token 序列、初始化失败、Lexer 诊断复制、预算超限 |
| M2.2 | 表达式 Parser：literal、ident、config ref、list、call、index、member、unary、binary | 优先级/结合性矩阵全部通过；链式比较拒绝；赋值不进入表达式 |
| M2.3 | 语句 Parser：变量声明、赋值、block、if/while/for-in/return/break/continue | 函数体内全部语句可解析；嵌套块与 else-if 正确 |
| M2.4 | 顶层声明：函数、target、target 字段、`@import`、`@include`、顶层 if | 完整模块文件可解析；导入/包含只记录不加载 |
| M2.5 | 集成与压力测试：文档示例、深嵌套、限制、失败回滚、确定性随机输入 | 失败路径不发布 AST、无泄漏；Debug/Release 全通过 |

每个子阶段先写测试，再实现功能；不得跨阶段合并。M2.0 的本文档是编码前置条件。

M2.0 状态：已完成。`cmk_ast.c/h`、`cmk_limits.c/h` 与 `CMKAST` 套件（10 个用例）已落地。
M2.1 状态：已完成。`cmk_parser.h`、`cmk_parser_internal.h`、`cmk_parser.c` 与 `CMKPARSER` 套件（11 个用例）已落地。
M2.2–M2.5 状态：已完成。表达式、语句、顶层声明、预算/压力/确定性随机输入测试分别落在 `cmk_parser_expr.c`、`cmk_parser_stmt.c`、`cmk_parser_decl.c` 与 `CMKEXPR`、`CMKSTMT`、`CMKDECL`、`CMKSTRESS` 套件；Parser 已能解析完整模块。
M3/M4 状态：已完成。语义与运行时边界见 [CMK 语义与运行时设计](CMK语义与运行时设计.md)；下一步进入 M5。

## 9. 测试计划

### 9.1 套件与编号

| 套件 | 用例编号 | 内容 |
|---|---|---|
| `CMKAST` | `AST-001` 至 `AST-010` | AST 模型、所有权、校验、visitor、顶层上下文约束 |
| `CMKPARSER` | `PARSER-001` 至 `PARSER-011` | 基础设施、诊断、预算、Token 缓冲 |
| `CMKEXPR` | `EXPR-001` 至 `EXPR-009` | 表达式、优先级、调用/索引/成员、配置引用、INT64 边界、复杂组合 |
| `CMKSTMT` | `STMT-001` 至 `STMT-007` | 语句、控制流、悬空 else、赋值 |
| `CMKDECL` | `DECL-001` 至 `DECL-006` | 导入/包含、函数、target、顶层 if、完整模块 |
| `CMKSTRESS` | `STRESS-001` 至 `STRESS-006` | 语法/表达式/类型深度、预算、大量声明、失败复用、确定性随机输入 |
| `CMKVALUE` | `VAL-001` 至 `VAL-005` | 类型、字符串/列表/动作所有权、目标引用、文本化 |
| `CMKBUILTIN` | `BLT-001` 至 `BLT-005` | 注册表、纯计算、host_info、动作构造、find_files、错误格式 |
| `CMKSEMANTIC` | `SEM-001` 至 `SEM-006` | 作用域、类型、函数、内建、target、配置、控制流 |
| `CMKRUNTIME` | `RT-001` 至 `RT-007` | 表达式、函数/递归、while/for、内建、target draft、运行时错误 |
| `CMKPROGRAM` | `PRG-001` 至 `PRG-005` | 模块加载、导入函数/target、通配/别名、@include、循环导入 |

现有 `CMK` 词法套件保持不变。`tests/test.c` 增加 `CMKAST`、`CMKPARSER`、`CMKEXPR`、`CMKSTMT`、`CMKDECL`、`CMKSTRESS`、`CMKVALUE`、`CMKBUILTIN`、`CMKSEMANTIC`、`CMKRUNTIME`、`CMKPROGRAM` 套件入口。

### 9.2 AST 测试

- `cmk_ast_create` / `cmk_ast_destroy` 成对；
- 手工创建 `module → var_decl → type/literal`，用 visitor 遍历；
- 字符串复制：修改源缓冲区后 AST 字符串不变；
- 文件路径复制：修改源路径后 AST 文件路径不变；
- `cmk_ast_append_child` 拒绝重复挂接、拒绝根节点作为子节点、拒绝 `parent == child`、拒绝形成祖先环、拒绝无效 ID；
- 节点/字符串 ID 越界返回 NIL；
- `cmk_ast_validate` 检测孤儿、环、子节点数量错误、类型节点子节点错误；
- 顶层上下文：普通 `IF` / `BLOCK` 拒绝 target，顶层 `IF` / 顶层 `BLOCK` 接受 target；`MODULE` 中的 `IF` 必须为顶层结构；
- AST 销毁后重复销毁不允许（调用方契约）。

### 9.3 Parser 测试

- 基础：空文件、只有注释、EOF、非法首 Token、Parser init 失败、重复 parse、out_ast 置空；
- 表达式：表驱动优先级/结合性；`a + b * c`、`a * b + c`、`a - b - c`、`!-a`、`-a * b`、`a == b == c`、`a < b < c` 拒绝、`a == b < c` 允许；
- 复杂组合：`a.b[1].c + f(1, 2) * -3`、`a < b && c >= d || e == f` 的结构断言；
- 整数边界：`INT64_MAX`、`-9223372036854775808` 作为 `INT64_MIN` 字面量、`9223372036854775808` 与 `-9223372036854775809` 拒绝；
- 调用：`f()`、`f(a, b)`、`f()[0]`、`common.core`、`(f)()` 拒绝、`f()()` 拒绝；
- 列表：`{}`、`{a}`、`{a, b,}`、嵌套列表；
- 配置引用：`@{a}`、`@{a.b[0]}`、`@{a..b}` 拒绝、`@{a[1x]}` 拒绝；
- 语句：变量声明、赋值、`_ = f()`、block、if/else if/else、while、for-in、return、break/continue；
- 声明：函数（void/非 void、参数）、target 字段、`@import func`、`@import func *`、`@import target ... as`、`@include`、顶层 if；
- 错误：`a = b = c` 拒绝、`if (x = 1)` 拒绝、target 内出现函数声明拒绝、顶层 if 分支出现非 target 声明拒绝；
- 类型深度：`list<list<int>>` 恰好达到限制、`list<list<list<int>>>` 超过限制、32 层 `list<>` 极端输入不崩溃；
- 诊断：Lexer 错误复制、Parser 错误位置、首错终止、Parser 不修改 Lexer 诊断；
- 失败路径：任何失败 `*out_ast == NULL`，成功 AST 可 validate；
- 预算：逐项把限制设小，检查固定错误消息；0 关闭限制；
- 随机输入：固定种子随机字节 → Lexer → Parser；失败不崩溃、不发布 AST，成功 AST validate 并销毁。

### 9.4 测试侧序列化

测试通过 `cmk_ast_visit` 生成固定 S-expression 文本，用于 golden 比较；序列化器只存在于 `tests/cmk/`，不是公共 API。正式调试输出如果以后需要，再提升为 `cmk_ast_dump`。

## 10. 后续与未决

- M2 不做多错误恢复；首错终止；
- M2 不实现 `CMK_AST_ACTION`；动作构造器由 M3 分类；
- M2 不把配置路径拆成 AST 段；M3 解析 `CMK_AST_CONFIG_REF.path`；如果 M3 需要共享段结构，再在不改变 `CONFIG_REF` 节点的前提下增加段表，M2 不提前引入；
- M2 不加载 `@include`/`@import` 文件；M3/Program 负责模块加载、循环检测和符号表；
- M2 不做常量折叠、类型检查、目标闭包；
- `CCINT64_MIN` 已通过 `-9223372036854775808` 的特殊字面量路径支持，不再是已知限制；
- Parser 预算默认值如果与项目实际规模冲突，在 M2.0 编码前调整本表；
- AST 公开访问接口稳定；节点内部布局允许后续在保持 API 的前提下调整。

## 11. 与现有文档的关系

- [CMK 词法与语法](CMK词法与语法.md)：已同步更新本文 §4 的 EBNF、优先级、赋值语句化、链式比较、调用限制与顶层 if；
- [CMK 实现细则](CMK实现细则.md)：文件布局加入 `cmk_limits.h` 与拆分后的 Parser 文件；§10 增加 Parser 资源预算说明；
- [开发手册](开发手册.md)：挂接本文链接；
- M2.0 的 `cmk_ast`、`cmk_limits` 与 `CMKAST` 测试已按本文落地；
- M2.1 的 Parser 基础设施与 `CMKPARSER` 测试已落地；
- M2.2–M2.5 的表达式、语句、声明、预算/压力/确定性随机输入测试已落地；M2 完成；
- M3/M4 的 Value、Builtin、Semantic、Runtime、Program 测试已落地；下一步进入 M5。
