# CMK 词法与语法

本文冻结 `.cmk` 的词法规则与语法骨架，作为 Lexer、Parser 与语义分析的共同依据。[CMK 文件规范](CMK文件规范.md) 描述语言能力与示例；本文只补充实现所需的形式化边界。未在规范中明确的细节，按 C 语言习惯处理，并在本文记录。

## 1. 阶段边界

| 阶段 | 输入 | 输出 | 不负责 |
|---|---|---|---|
| Lexer | 源码字节、文件路径 | token 序列与词法诊断 | 名称解析、类型、目标图 |
| Parser | token 序列 | AST | 类型检查、符号解析 |
| Semantic | AST、配置变量树、内建签名表 | 已验证的 AST | 生成 IR、执行进程 |

Lexer 与 Parser 是首错终止：遇到第一个词法或语法错误立即返回，不尝试恢复。

## 2. 源码与位置

- 源文件按字节读入，内部保存为带结尾 0 的只读缓冲区；
- 诊断位置使用文件路径、行号与字节列号，均从 1 开始；
- `\n`、`\r`、`\r\n` 都按一次换行计；CRLF 作为一个换行，列号在换行后回到 1；
- 列号按字节递增，非 ASCII 字符（如中文注释）占多个列；
- 文件中的 NUL 字节视为二进制内容，词法阶段报错；
- UTF-8 BOM、编码合法性由装载模块在交给 Lexer 前处理；Lexer 不主动跳过 BOM。

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
- 字符串可按字节包含 0，长度以 `string_len` 为准。

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

## 5. 语法

```text
file            := { import | include | top_statement } ;

import          := "@import" ( func_import | target_import ) "from" string ";" ;
func_import     := "func" ( "*" | ident { "," ident } ) [ "as" ident ] ;
target_import   := "target" ident { "," ident } "as" ident ;
include         := "@include" string ";" ;

top_statement   := declaration | if_statement ;

declaration     := variable_decl | function_decl | target_decl ;
variable_decl   := [ "const" ] type ident "=" expression ";" ;

type            := "string" | "int" | "bool" | "float" | "action"
                 | "target_ref" | "list" "<" type ">" ;

function_decl   := "func" ident "(" [ parameter { "," parameter } ] ")"
                   "->" ( type | "void" ) block ;
parameter       := type ident ;

target_decl     := "target" ident "(" ")" block ;

block           := "{" { statement } "}" ;

statement       := variable_decl | block | if_statement | while_statement
                 | for_statement | "break" ";" | "continue" ";"
                 | return_statement | expression_statement ;

if_statement    := "if" "(" expression ")" statement
                   [ "else" statement ] ;
while_statement := "while" "(" expression ")" statement ;
for_statement   := "for" "(" type ident "in" expression ")" statement ;
return_statement := "return" [ expression ] ";" ;
expression_statement := expression ";" ;

expression      := assignment ;
assignment      := ( ident | discard ) ( "=" | "+=" ) assignment
                 | logical_or ;
logical_or      := logical_and { "||" logical_and } ;
logical_and     := equality { "&&" equality } ;
equality        := relational { ( "==" | "!=" ) relational } ;
relational      := additive { ( "<" | ">" | "<=" | ">=" ) additive } ;
additive        := multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative  := unary { ( "*" | "/" | "%" ) unary } ;
unary           := ( "!" | "-" ) unary | postfix ;
postfix         := primary { call_suffix | index_suffix | member_suffix } ;
call_suffix     := "(" [ expression { "," expression } ] ")" ;
index_suffix    := "[" expression "]" ;
member_suffix   := "." ident ;
primary         := int | float | string | "true" | "false"
                 | config_ref | ident | "(" expression ")"
                 | list_literal ;
list_literal    := "{" [ expression { "," expression } [ "," ] ] "}" ;
config_ref      := "@{" config_path "}" ;
config_path     := key { "." key | "[" int "]" } ;
discard         := "_" ;
```

说明：

- `target` 声明使用空参数列表 `()`，不是普通函数；
- 列表字面量使用 `{}`，与代码块在语法上由上下文区分：声明右侧、实参位置与动作列表位置按表达式解析；
- 表达式语句的返回值必须被使用或赋给 `_`，由语义层强制；
- `assignment` 的左值目前只允许变量名与 `_`；列表元素、目标引用不是可赋值对象；
- `for` 只有 `for-in` 形式，没有 C 的三段式 `for`；
- 顶层 `if` 只用于选择声明；语义层限制其分支内容，语法层允许普通语句；
- 目标体使用普通 `block`，语义层限制为四个字段的 `list<T>` 声明。

## 6. 优先级与结合性

从高到低：

| 级别 | 运算符 | 结合性 |
|---|---|---|
| 1 | 调用 `()`、索引 `[]`、成员 `.` | 左 |
| 2 | 一元 `!` `-` | 右 |
| 3 | `*` `/` `%` | 左 |
| 4 | `+` `-` | 左 |
| 5 | `<` `>` `<=` `>=` | 左 |
| 6 | `==` `!=` | 左 |
| 7 | `&&` | 左 |
| 8 | `\|\|` | 左 |
| 9 | `=` `+=` | 右 |

- 赋值是表达式但不是普通右值；解析结果由语义层限制为语句级赋值；
- 比较与逻辑运算的结果是 `bool`；`&&`、`||` 短路求值；
- 没有隐式类型转换；`+` 的字符串拼接与列表连接由语义层按类型分派；
- 列表字面量 `{}` 的元素类型由声明、返回类型或目标字段上下文确定。

## 7. 语法之外的约束

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
| `src/cmk/cmk_source.c/h` | 源码缓冲区、路径与内容的成对创建/释放、源码位置 |
| `src/cmk/cmk_token.h` | token 类型、token 结构与类型名称 |
| `src/cmk/cmk_diagnostic.c/h` | 诊断对象：级别、文件、行号、列号、动态消息、输出 |
| `src/cmk/cmk_lexer.c/h` | 词法分析器：空白、注释、标识符、关键字、数字、字符串、指令、配置引用、运算符 |

测试：

| 套件 | 用例 | 覆盖 |
|---|---|---|
| `CMK` | `CMK-001` 至 `CMK-018` | token 序列与位置、关键字全表、运算符、注释、字符串转义、数字、指令、配置引用、`@include`/`@import` 语句、NUL、CRLF、EOF、诊断输出、源码缓冲区所有权 |

构建脚本递归收集 `src/**/*.c`；`scripts/build_test.bat` 显式加入 `tests\cmk`，`scripts/build_test.sh` 已按目录规则收集全部测试源码。

## 9. 诊断

词法/语法阶段统一前缀：

```text
构建脚本解析失败：<文件>:<行>:<列>: <说明>
```

- 有文件与行号但无列号时省略列号；
- 无位置信息时只输出消息；
- 诊断消息动态分配，不设固定长度上限；
- Lexer 只记录诊断，由调用方决定何时输出，便于后续 Parser 在单次错误中统一打印；
- token 数量、标识符长度、字符串长度不设语言层配额；`CCUINT32` 字段的表示上限在越界时给出明确诊断。
