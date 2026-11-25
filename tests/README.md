# 测试

## 目录结构

```text
test.bat / test.sh        测试运行入口
scripts/build_test.bat/.sh 编译测试程序（由 build.bat / build.sh 调用）
tests/
  test.c                  测试入口：套件表、命令行参数、退出码
  test_header.h           测试程序内部头文件（套件声明）
  test_harness.c/.h       用例、断言、统计与 stdout 捕获
  cmd/test_cmd.c          命令行解析用例
  mc/test_mc_locate.c   MC 文件检查与定位用例
  mc/test_mc_parse.c    MC 内容解析用例
  mk/test_mk_lexer.c    MK 词法分析用例
  mk/test_mk_ast.c      MK AST 模型用例
  mk/test_mk_parser_base.c MK Parser 基础设施用例
  mk/test_mk_parser_expr.c MK 表达式解析用例
  mk/test_mk_parser_stmt.c MK 语句解析用例
  mk/test_mk_parser_stress.c MK Parser 压力与随机输入用例
  mk/test_mk_value.c    MK Value 模型用例
  mk/test_mk_builtin.c  MK Builtin 注册表与标准内建用例
  mk/test_mk_semantic.c MK Semantic 用例
  mk/test_mk_runtime.c  MK Configure 运行时用例
  mk/test_mk_program.c  MK 模块加载与 Program 用例
  mk/test_mk_process.c  MK Process
  mk/test_mk_executor.c MK Executor
  mk/mk_parser_test_util.c/.h Parser 测试辅助
  mk/mk_test_context.c/.h Semantic/Runtime 测试辅助
  vars/test_ccvar.c       全局变量树用例
  data/mc/               MC 用例 fixture（不参与测试程序编译）
  data/mk/program/       MK Program 用例 fixture（不参与测试程序编译）
```

产物：`out/<平台>_bin/ccmake_test(.exe)`。

## 运行

```bat
build.bat
test.bat
test.bat CMD
test.bat MC
test.bat PARSE
test.bat VARS
test.bat MK
test.bat MKAST
test.bat MKPARSER
test.bat MKEXPR
test.bat MKSTMT
test.bat MKSTRESS
test.bat MKVALUE
test.bat MKBUILTIN
test.bat MKSEMANTIC
test.bat MKRUNTIME
test.bat MKPROC
test.bat MKEXEC
test.bat MKPROGRAM
```

```bash
bash build.sh
bash test.sh
bash test.sh CMD
bash test.sh MC
bash test.sh PARSE
bash test.sh VARS
bash test.sh MK
bash test.sh MKAST
bash test.sh MKPARSER
bash test.sh MKEXPR
bash test.sh MKSTMT
bash test.sh MKSTRESS
bash test.sh MKVALUE
bash test.sh MKBUILTIN
bash test.sh MKSEMANTIC
bash test.sh MKRUNTIME
bash test.sh MKPROGRAM
```

`test.bat` / `test.sh` 只检查产物是否存在并运行，不负责编译。

## 覆盖范围

- 帮助、版本与动作类参数的优先级；
- 必填项、缺值、空值、非法值与位置参数；
- `-j`：指定值、`0` 自动检测、非数字、溢出；
- `-c/-f/-C/-o/-t` 字段写入、长格式、重复参数覆盖；
- `-D`：一个 `-D` 多个键值对、重复键以后者为准、值中含 `=`；
- `--dump-config` 的动作请求字段；
- MC 定位：单文件、链式/菱形/重复 include、循环、注释与字符串、非法指令、缺失目标、任意后缀、根错误、BOM/CRLF/NUL/UTF-16、`-C`、绝对路径、空格路径、Windows 大小写去重、路径规范化与资源清理；
- MC 解析：基本类型、嵌套对象、文件顺序、对象浅合并、`${ref}`、嵌套数组、数组对象、重复键、空容器、语法错误、`${find}`、内嵌 `${ref}`、注释、`-D` 覆盖与非法键，值深度边界、5000 层显式值帧栈、MC 动态追加边界、token/数组/对象预算、include 文件/边/深度预算、`${find}` 结果预算、浮点范围、引号内建函数、失败后复用；
- 全局变量树：路径写入、覆盖、对象合并、数组、子树删除、键名规则、大量同层键、清空复用；
- MK 词法：token 序列、位置与 `offset`/`width` 边界、关键字/运算符、注释与 EOF、字符串转义与解码缓冲生命周期、空字符串、数字与指数错误位置、`@include`/`@import` 与 `@{...}` 配置引用、指令名称边界、NUL/CRLF、源码缓冲区所有权与尺寸边界、非法初始化参数、行号/列号表示上限、确定性随机字节输入健壮性；
- MK AST：根节点与生命周期、文件路径/字符串复制、节点与字符串 ID 越界、父子/兄弟链接、重复挂接与祖先环拒绝、visitor 先序遍历与中断、post-parse 校验的孤儿/环/子节点类型/字符串引用/类型节点错误、函数/导入节点布局、类型分类、顶层上下文约束，以及 `MKLimits` 默认值；
- MK Parser 基础设施：空模块解析与事务发布、注释后的 EOF 位置、未支持顶层 Token、Lexer 诊断复制、初始化参数、Token/深度/AST 节点/声明预算、单 Token 前瞻、`expect` 错误、Parser 清理；
- MK 表达式：字面量与列表、优先级/结合性、调用/索引/成员、配置引用、数值边界与 INT64_MIN、复杂表达式结构、一元与丢弃符、赋值语句化；
- MK 语句：变量/赋值、if/else、while/for-in、return/break/continue、悬空 else、表达式语句、语句错误；
- MK Parser 压力：语法/表达式/类型深度、各类预算、5000 层显式栈类型/一元/嵌套块/嵌套 if/括号/列表/调用、大量声明、失败后复用、确定性随机字节输入；
- MK Value：类型、字符串所有权、列表深复制与文本化；
- MK Builtin：注册表、纯计算、host_info、动作构造、find_files、error 格式与参数错误；
- MK Semantic：模块/函数/块作用域、类型、const、函数与内建调用、配置引用、控制流返回检查、5000 层显式语句/表达式栈；
- MK Runtime：模块级变量、while、函数/递归、列表/字符串、配置、for/break/continue、索引/除零/error 运行时错误、`MKExecutionPolicy`（步数/递归/时间/find 深度）、5000 层显式语句/表达式/调用/尾递归、线性/分支递归程序栈、通用语句上下文 VM、5000 层非 return 深递归与循环内递归；
- MK Program：根模块、跨模块函数、通配/别名导入、`@include`、循环导入、导入错误、Program 执行策略、环境变量策略、进程/捕获策略与非法环境变量回退；
- MK Executor：mkdir/copy/move/remove 与 exec 动作。

测试直接调用 `parse_cmd` / `mc_locate` / `mc_parse` / MK Program API，不经过 `main`；命令行用例不受 `.mc`/`.mk` 解析影响。

## 断言方式

`CC_CASE` / `CC_CASE_END` 包裹用例，`CC_CHECK`、`CC_CHECK_EQ_I64`、`CC_CHECK_EQ_STR`、`CC_CHECK_EQ_MEM` 断言。

解析期间的 stdout 通过**管道**（`_pipe` / `pipe` + `dup2`）临时接管后读回，并剥离终端颜色控制序列，因此可以逐字校验中文提示与行数；该方式不依赖 `TMP`/`TEMP` 或临时文件。

套件启动时先做一次 `CMD-HARNESS-000` 捕获自检：若当前环境无法接管 stdout，只报一条失败并中止，同时在 stderr 打印失败的系统调用与 errno，不会把全部用例误报为失败。

测试程序链接 `src/` 下除 `main.c` 外的全部源码；`tests/data/` 仅存放 fixture，不参与编译。测试程序与 `src/` 使用同一套编译参数（Windows 下都带 `-fexec-charset=GBK`），中文期望值可以直接写成字符串字面量。
