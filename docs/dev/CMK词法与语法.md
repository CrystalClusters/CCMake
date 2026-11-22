# CMK 词法与语法

本文冻结 `.cmk` 的词法规则与语法骨架，作为 Lexer、Parser 与语义分析的共同依据。[CMK 文件规范](CMK文件规范.md) 描述语言能力与示例；[CMK AST 与解析设计](CMK%20AST与解析设计.md) 冻结 M2 的 AST 数据模型与 Parser 策略。未在规范中明确的细节，按 C 语言习惯处理，并在本文记录。

## 1. 阶段边界

| 阶段 | 输入 | 输出 | 不负责 |
|---|---|---|---|
| Lexer | 源码字节、文件路径 | token 序列与词法诊断 | 名称解析、类型、目标图 |
| Parser | token 序列 | AST | 类型检查、符号解析、求值、加载模块 |
| Semantic | AST、配置变量树、内建签名表 | 已验证的 AST | 生成 IR、执行进程 |

Lexer 与 Parser 是首错终止：遇到第一个词法或语法错误立即返回，不尝试恢复。

## 2. 源码与位置

- 源文件按字节读入，内部保存为带结尾 0 的只读缓冲区；
- 诊断位置使用文件路径、行号与字节列号，均从 1 开始；
- `\n`、`\r`、`\r\n` 都按一次换行计；CRLF 作为一个换行，列号在换行后回到 1；
- 列号按字节递增，非 ASCII 字符（如中文注释）占多个列；
- 文件中的 NUL 字节视为二进制内容，词法阶段报错；
- UTF-8 BOM、编码合法性由装载模块在交给 Lexer 前处理；Lexer 不主动跳过 BOM；
- 源码长度使用 `CCUINT64`，行号与列号使用 `CCUINT32`；位置推进经过显式检查，超过表示上限时报告“行号/列号超出表示上限”，不允许回绕；
- 文件路径可以为 NULL，表示匿名源码；诊断输出用 `(.cmk)` 代替文件路径。

## 3. 词法规则

### 3.1 空白与注释

| 输入 | 处理 |
|---|---|
| 空格、`\t`、`\f`、`\v` | 跳过，列号递增 |
| `\n` / `\r` / `\r\n` | 更新行号，列号回到 1 |
| `// ...` | 跳到行尾，不消费换行 |
| `/* ... */` | 跳到 `*/`；未闭合在注释起点报错 |
| `"` | 进入字符串扫描 |

- 注释只作为 token 之间的空白，不产生 token；
- 块注释不嵌套，`/* /* */` 在第一个 `*/` 结束；
- 注释内出现 NUL 字节按二进制内容报错。

### 3.2 标识符与关键字

```text
ident := [A-Za-z_][A-Za-z0-9_]*
```

标识符仅使用 ASCII，大小写敏感。关键字表：

| 类别 | 关键字 |
|---|---|
| 类型 | `string` `int` `bool` `float` `list` `action` `target_ref` `void` |
| 声明 | `func` `target` `const` |
| 控制流 | `if` `else` `for` `in` `while` `break` `continue` `return` |
| 导入 | `@include` `@import` `from` `as` |
| 字面量 | `true` `false` |
| 丢弃符 | `_` |

- 关键字拥有独立 token 类型，Parser 不再做文本比较；
- `_` 单独出现时是丢弃 token，`_x`、`__` 仍是普通标识符；
- 保留字不能作为变量名、函数名或目标名，由 Parser/语义层拒绝。

### 3.3 数字字面量

```text
int   := digit+
float := digit+ "." digit* [exponent]
       | "." digit+ [exponent]
       | digit+ exponent
exponent := ("e" | "E") ["+" | "-"] digit+
```

- 只接受十进制；十六进制、八进制、二进制与类型后缀不属于当前语法；
- 负号是独立的一元运算符，不并入字面量；
- `1.`、`.5`、`1e3`、`2.5E-2` 合法；
- 数字后紧跟标识符字符或 `.` 属于词法错误，例如 `123abc`、`1.2.3`；
- 整数转 `CCINT64`、浮点转 `double` 的范围检查在 Parser/语义层完成。

### 3.4 字符串与转义

- 起点与终点都是 `"`；
- 字符串不能跨行；需要换行时写 `\n`；
- 未闭合字符串在起始引号处报错；
- 支持的反斜杠转义：

| 转义 | 结果 |
|---|---|
| `\\` | 反斜杠 |
| `\"` | 双引号 |
| `\'` | 单引号 |
| `\n` | 换行 |
| `\r` | 回车 |
| `\t` | 制表 |
| `\a` `\b` `\f` `\v` | 对应控制字符 |
| `\0` | 0 字节 |

- 其他转义序列是词法错误；
- token 同时提供原始切片（`text`/`len`）与解码结果（`string`/`string_len`）；解码结果由 Lexer 持有，下一次读取前有效；
- 字符串可按字节包含 0，长度以 `string_len` 为准；
- **Parser 约定**：Parser 读到字符串 token 时立即把 `string`/`string_len` 复制到 AST 节点拥有的内存；不得先缓存 token 序列、稍后再统一构造 AST，因为 Lexer 的解码缓冲会在下一次读取时被复用。

### 3.5 指令与配置引用

```text
@include "路径" ;
@import func ...
@import target ... as 别名 ;
@{配置路径}
```

- `@` 后读取 `[A-Za-z_][A-Za-z0-9_]*`；
- 名称等于 `include` / `import` 时产生对应 token，token 文本不含 `@`；
- 其他名称报“未知的 @ 指令”；
- `@` 后紧跟 `{` 时读取配置引用，token 文本为花括号内部原样字节；
- 配置引用内部只允许标识符字符、`.`、`[`、`]`；不允许空白与换行；
- 空配置引用（`@{}`）与未闭合配置引用是词法错误。

配置路径的完整校验由 Parser 完成，规则为：

```text
config_path := key ( "." key | "[" digit+ "]" )*
```

### 3.6 运算符与标点

| 类别 | token |
|---|---|
| 算术 | `+` `-` `*` `/` `%` |
| 赋值 | `=` `+=` |
| 比较 | `==` `!=` `<` `>` `<=` `>=` |
| 逻辑 | `&&` `\|\|` `!` |
| 分隔 | `(` `)` `{` `}` `[` `]` `,` `;` `.` |
| 返回类型 | `->` |

- 单字符 `&`、`|` 不是合法 token，报错并提示使用 `&&`、`||`；
- 当前语法没有 `++`、`--`、位运算、三目运算、`::`、`=>`；
- 其他未列出的字节报“无法识别的字符”，诊断包含十六进制字节值。

## 4. Token 类型

`src/cmk/cmk_token.h` 定义全部 token。分类如下：

| 分类 | 类型 |
|---|---|
| 结束 | `CMK_TOK_EOF` |
| 标识符与字面量 | `IDENT` `INT` `FLOAT` `STRING` |
| 类型关键字 | `KW_STRING` `KW_INT` `KW_BOOL` `KW_FLOAT` `KW_LIST` `KW_ACTION` `KW_TARGET_REF` `KW_VOID` |
| 声明与导入关键字 | `KW_TARGET` `KW_FUNC` `KW_CONST` `KW_FROM` `KW_AS` |
| 控制流关键字 | `KW_IF` `KW_ELSE` `KW_FOR` `KW_IN` `KW_WHILE` `KW_BREAK` `KW_CONTINUE` `KW_RETURN` |
| 布尔与丢弃 | `KW_TRUE` `KW_FALSE` `UNDERSCORE` |
| 指令 | `AT_INCLUDE` `AT_IMPORT` `CONFIG_REF` |
| 标点 | `LPAREN` `RPAREN` `LBRACE` `RBRACE` `LBRACKET` `RBRACKET` `COMMA` `SEMI` `DOT` |
| 运算符 | `PLUS` `MINUS` `STAR` `SLASH` `PERCENT` `ASSIGN` `PLUS_ASSIGN` `EQ` `NE` `LT` `GT` `LE` `GE` `AND` `OR` `NOT` `ARROW` |

### 4.1 Token 字段与边界

| 字段 | 含义 |
|---|---|
| `type` | token 类型 |
| `offset` | 完整词法单元在源码中的起始字节偏移 |
| `width` | 完整词法单元字节宽度，包含引号、`@`、`@{` 与 `}`；EOF 为 0 |
| `text` / `len` | 源码切片，范围按 token 类型约定，见下表；不保证以 0 结尾 |
| `string` / `string_len` | 仅字符串：解码后的字节，借自 Lexer |
| `line` / `col` | 完整词法单元的起始位置，行号与字节列号从 1 开始 |

`text`/`len` 不是所有 token 的完整词法单元，Parser 若需要完整源码范围必须使用 `offset`/`width`：

| token | `text` / `len` 范围 | `offset` / `width` |
|---|---|---|
| 标识符、关键字、数字、运算符、标点 | 完整词法单元 | 完整词法单元 |
| 字符串 `STRING` | 去掉外层双引号后的原始字节 | 包含两个引号 |
| `@include` / `@import` | 不含 `@` 的指令名 | 包含 `@` |
| 配置引用 `CONFIG_REF` | `{` 与 `}` 之间的路径原文 | 包含 `@{` 与 `}` |
| `EOF` | `NULL` / 0 | 源码末尾；`width` 为 0 |

## 5. 语法

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

- `target` 声明使用空参数列表 `()`，不是普通函数；
- `target_body` 是独立语法入口，不是普通 `block`；字段直接挂在 `TARGET_DECL` 下；
- 变量声明是 `block_item`，不是 `statement`：`if (cond) int x = 1;` 语法错误，需要写成 `if (cond) { int x = 1; }`；
- 列表字面量使用 `{}`，与代码块由 Parser 按上下文入口区分，Lexer 不做推断；
- 表达式语句的返回值必须被使用或赋给 `_`，由语义层强制；
- 赋值是语句级结构，只允许 `ident` 或 `_` 作左值；不允许 `a = b = c`、`if (x = 1)`、`f(x = 1)`；
- 比较禁止链式：`a < b < c` 是语法错误；`a == b == c` 仍按左结合解析；
- 调用只允许裸标识符：`f()`、`f(a)` 合法，`(f)()`、`f()()` 语法拒绝；
- `for` 只有 `for-in` 形式，没有 C 的三段式 `for`；
- 顶层 `if` 使用 `top_if`，分支只允许 `target_decl` 或嵌套 `top_if`；函数/块内 `if` 使用普通 `if_statement`；
- 目标字段语法接受任意类型与字段名，字段白名单与 `list<T>` 类型由语义层检查；
- `@include` / `@import` 只记录路径，Parser 不加载文件；
- `{}` 的双重含义由 Parser 提供 `parse_block`、`parse_target_fields`、`parse_list` 三个入口，不使用通用 `parse_braces`，也不做大量 Token 回溯；
- Lexer 与 Parser 都保持首错终止；M2 初期不引入多错误恢复或局部重同步。

## 6. 优先级与结合性

从高到低：

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
| — | `=` `+=` | 语句级，不进入表达式 |

- 赋值是语句级结构，不是表达式；`a = b = c` 与条件内赋值都被语法拒绝；
- 关系比较禁止链式：`a < b < c` 是语法错误；`a == b == c` 仍按左结合解析；
- 调用只允许裸标识符；`(f)()`、`f()()` 是语法错误；
- 比较与逻辑运算的结果是 `bool`；`&&`、`||` 短路求值；
- 没有隐式类型转换；`+` 的字符串拼接与列表连接由语义层按类型分派；
- 列表字面量 `{}` 的元素类型由声明、返回类型或目标字段上下文确定。

Pratt 绑定力：

| 运算符 | `lbp` |
|---|---:|
| `\|\|` | 10 |
| `&&` | 20 |
| `==` `!=` | 30 |
| `<` `>` `<=` `>=` | 40 |
| `+` `-` | 50 |
| `*` `/` `%` | 60 |

关系层级在同一个 Pratt 调用内使用 `relational_seen` 标记；已经解析过一个关系运算符后再遇到关系运算符直接报错。括号或嵌套表达式使用新的标记。

## 7. 语法之外的约束

### 7.1 语法层直接拒绝

- `a = b = c`、`if (x = 1)`、`f(x = 1)`：赋值只能作为语句；
- `a < b < c`：不支持链式比较；
- `(f)()`、`f()()`：调用只允许裸标识符；
- 顶层 `if` 分支出现函数声明、变量声明、导入或包含：顶层 if 只选择 target；
- `target` 体出现普通语句、函数声明或嵌套 target：target 体只允许字段声明。

### 7.2 语义层拒绝

以下内容语法接受，但由后续阶段拒绝：

- 未声明的标识符、重复声明、未定义的函数与内建名覆盖；
- 变量重赋值类型不匹配、`const` 重新赋值；
- 非 `void` 函数缺少返回值、`return;` 用于非 `void` 函数；
- `void` 作为变量类型、裸 `list`、嵌套列表、列表元素类型不一致；
- `target` 出现在函数、循环或动作列表内，目标名重复，字段名/字段类型非法；
- `depends_on` 引用未导入的目标，跨模块目标未写 `别名.目标名`；
- `@{...}` 路径不存在、对象不能字符串化、配置类型不匹配；
- `command` / `mkdir` / `copy` / `move` / `remove` 的参数类型或数量错误；
- 有返回值的调用未赋值给变量或 `_`。

## 8. 实现与测试

M1 已落地：

| 文件 | 职责 |
|---|---|
| `src/cmk/cmk_source.c/h` | 源码缓冲区、路径与内容的成对创建/释放、源码位置；拒绝超出 `size_t` 的尺寸 |
| `src/cmk/cmk_token.h` | token 类型、token 结构（含 `offset` / `width`）与类型名称 |
| `src/cmk/cmk_diagnostic.c/h` | 诊断对象：级别、文件、行号、列号、动态消息、输出 |
| `src/cmk/cmk_lexer.c/h` | 词法分析器：空白、注释、标识符、关键字、数字、字符串、指令、配置引用、运算符；初始化契约与位置溢出检查 |

测试：

| 套件 | 用例 | 覆盖 |
|---|---|---|
| `CMK` | `CMK-001` 至 `CMK-027` | token 序列、位置与 `offset`/`width` 边界、关键字全表、运算符、注释与 EOF 边界、字符串转义与解码缓冲生命周期、空字符串、连续转义、数字与指数错误位置、指令名称边界、超长指令、配置引用、`@include`/`@import` 语句、NUL、CRLF、EOF、诊断输出、源码缓冲区所有权与尺寸边界、非法初始化参数、行号/列号表示上限、确定性随机字节输入健壮性 |

构建脚本递归收集 `src/**/*.c`；`scripts/build_test.bat` 显式加入 `tests\cmk`，`scripts/build_test.sh` 已按目录规则收集全部测试源码。

`CMK-027` 是确定性的随机字节输入测试，固定种子、固定轮数，用于保证 Lexer 在各种字节组合下不崩溃、不越界；后续可以在同一入口接 libFuzzer 做长时间覆盖引导测试。

M2.0 的 AST 数据模型与预算默认值已实现（`cmk_ast.c/h`、`cmk_limits.c/h`、`CMKAST` 测试套件）；Parser 尚未开始。M2 的设计仍以 [CMK AST 与解析设计](CMK%20AST与解析设计.md) 为准。

| 套件 | 用例 | 覆盖 |
|---|---|---|
| `CMKAST` | `AST-001` 至 `AST-009` | 根节点与生命周期、文件/字符串复制、节点与字符串越界、父/子/兄弟链接、挂接规则、visitor、校验失败路径、函数/target/导入节点布局、类型分类、预算默认值 |

### 8.1 Lexer 输入契约

| 参数 | 规则 |
|---|---|
| `file` | 借用；可为 NULL，表示匿名源码，诊断显示 `(.cmk)` |
| `data` | 借用；`size == 0` 时可为 NULL，否则必须指向至少 `size` 字节的有效内存 |
| `size` | 必须能用 `size_t` 表示；NUL 检测、长度统计与位置推进都基于该值 |
| 生命周期 | Lexer 不复制源码；调用方保证 `file` / `data` 在 Lexer 清理前有效 |
| 初始化失败 | 返回 `CCFALSE`，Lexer 进入可预测的失败状态：仍可 `cmk_lexer_clear`，`cmk_lexer_next` 持续返回 `CCFALSE` |
| 常规入口 | `cmk_lexer_init_source`；裸指针入口只用于测试与特殊场景 |

## 9. 诊断

词法/语法阶段统一前缀：

```text
构建脚本解析失败：<文件>:<行>:<列>: <说明>
```

- 有文件与行号但无列号时省略列号；
- 无位置信息时只输出消息；
- 诊断消息动态分配，不设固定长度上限；
- Lexer 只记录诊断，由调用方决定何时输出，便于后续 Parser 在单次错误中统一打印；
- token 数量、标识符长度、字符串长度不设语言层配额；`CCUINT32` 字段的表示上限在越界时给出明确诊断；
- Parser 的实现资源预算见 [CMK AST 与解析设计](CMK%20AST与解析设计.md)，预算可设为 0 关闭，不属于语言语义限制；
- 行号或列号超过 `CCUINT32` 表示范围时，诊断分别为“行号超出表示上限”“列号超出表示上限”，不允许位置回绕；
- 源码尺寸超过 `size_t` 可表示范围时，`cmk_source_set` 返回失败，不进行分配或复制。
