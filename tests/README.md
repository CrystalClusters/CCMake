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
  ccm/test_ccm_locate.c   CCM 文件检查与定位用例
  ccm/test_ccm_parse.c    CCM 内容解析用例
  cmk/test_cmk_lexer.c    CMK 词法分析用例
  cmk/test_cmk_ast.c      CMK AST 模型用例
  cmk/test_cmk_parser_base.c CMK Parser 基础设施用例
  cmk/test_cmk_parser_expr.c CMK 表达式解析用例
  cmk/test_cmk_parser_stmt.c CMK 语句解析用例
  cmk/test_cmk_parser_decl.c CMK 声明解析用例
  cmk/test_cmk_parser_stress.c CMK Parser 压力与随机输入用例
  cmk/cmk_parser_test_util.c/.h Parser 测试辅助
  vars/test_ccvar.c       全局变量树用例
  data/ccm/               CCM 用例 fixture（不参与测试程序编译）
```

产物：`out/<平台>_bin/ccmake_test(.exe)`。

## 运行

```bat
build.bat
test.bat
test.bat CMD
test.bat CCM
test.bat PARSE
test.bat VARS
test.bat CMK
test.bat CMKAST
test.bat CMKPARSER
test.bat CMKEXPR
test.bat CMKSTMT
test.bat CMKDECL
test.bat CMKSTRESS
```

```bash
bash build.sh
bash test.sh
bash test.sh CMD
bash test.sh CCM
bash test.sh PARSE
bash test.sh VARS
bash test.sh CMK
bash test.sh CMKAST
bash test.sh CMKPARSER
bash test.sh CMKEXPR
bash test.sh CMKSTMT
bash test.sh CMKDECL
bash test.sh CMKSTRESS
```

`test.bat` / `test.sh` 只检查产物是否存在并运行，不负责编译。

## 覆盖范围

- 帮助、版本与动作类参数的优先级；
- 必填项、缺值、空值、非法值与位置参数；
- `-j`：指定值、`0` 自动检测、非数字、溢出；
- `-c/-f/-C/-o/-t` 字段写入、长格式、重复参数覆盖；
- `-D`：一个 `-D` 多个键值对、重复键以后者为准、值中含 `=`；
- `--list-targets` / `--dump-config` 的动作请求字段；
- CCM 定位：单文件、链式/菱形/重复 include、循环、注释与字符串、非法指令、缺失目标、任意后缀、根错误、BOM/CRLF/NUL/UTF-16、`-C`、绝对路径、空格路径、Windows 大小写去重、路径规范化与资源清理；
- CCM 解析：基本类型、嵌套对象、文件顺序、对象浅合并、`${ref}`、嵌套数组、数组对象、重复键、空容器、语法错误、`${find}`、内嵌 `${ref}`、注释、`-D` 覆盖与非法键，以及值深度边界、token/数组/对象预算、include 文件/边/深度预算、`${find}` 结果预算、浮点范围、引号内建函数、失败后复用；
- 全局变量树：路径写入、覆盖、对象合并、数组、子树删除、键名规则、大量同层键、清空复用；
- CMK 词法：token 序列、位置与 `offset`/`width` 边界、关键字/运算符、注释与 EOF、字符串转义与解码缓冲生命周期、空字符串、数字与指数错误位置、`@include`/`@import` 与 `@{...}` 配置引用、指令名称边界、NUL/CRLF、源码缓冲区所有权与尺寸边界、非法初始化参数、行号/列号表示上限、确定性随机字节输入健壮性；
- CMK AST：根节点与生命周期、文件路径/字符串复制、节点与字符串 ID 越界、父子/兄弟链接、重复挂接与祖先环拒绝、visitor 先序遍历与中断、post-parse 校验的孤儿/环/子节点类型/字符串引用/类型节点错误、函数/target/导入节点布局、类型分类、顶层上下文约束，以及 `CMKLimits` 默认值；
- CMK Parser 基础设施：空模块解析与事务发布、注释后的 EOF 位置、未支持顶层 Token、Lexer 诊断复制、初始化参数、Token/深度/AST 节点/声明预算、单 Token 前瞻、`expect` 错误、Parser 清理；
- CMK 表达式：字面量与列表、优先级/结合性、调用/索引/成员、配置引用、数值边界与 INT64_MIN、复杂表达式结构、一元与丢弃符、赋值语句化；
- CMK 语句：变量/赋值、if/else、while/for-in、return/break/continue、悬空 else、表达式语句、语句错误；
- CMK 声明：include/import、变量/函数、target 字段、顶层 if、完整模块与声明错误；
- CMK Parser 压力：语法/表达式/类型深度、各类预算、深嵌套块、大量声明、失败后复用、确定性随机字节输入。

测试直接调用 `parse_cmd` / `ccm_locate` / `ccm_parse`，不经过 `main`；命令行用例不受 `.ccm` 解析影响。

## 断言方式

`CC_CASE` / `CC_CASE_END` 包裹用例，`CC_CHECK`、`CC_CHECK_EQ_I64`、`CC_CHECK_EQ_STR`、`CC_CHECK_EQ_MEM` 断言。

解析期间的 stdout 通过**管道**（`_pipe` / `pipe` + `dup2`）临时接管后读回，并剥离终端颜色控制序列，因此可以逐字校验中文提示与行数；该方式不依赖 `TMP`/`TEMP` 或临时文件。

套件启动时先做一次 `CMD-HARNESS-000` 捕获自检：若当前环境无法接管 stdout，只报一条失败并中止，同时在 stderr 打印失败的系统调用与 errno，不会把全部用例误报为失败。

测试程序链接 `src/` 下除 `main.c` 外的全部源码；`tests/data/` 仅存放 fixture，不参与编译。测试程序与 `src/` 使用同一套编译参数（Windows 下都带 `-fexec-charset=GBK`），中文期望值可以直接写成字符串字面量。
