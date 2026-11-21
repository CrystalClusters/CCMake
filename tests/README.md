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
  data/ccm/               CCM 用例 fixture
```

产物：`out/<平台>_bin/ccmake_test(.exe)`。

## 运行

```bat
build.bat
test.bat
test.bat CMD
```

```bash
bash build.sh
bash test.sh
bash test.sh CMD
```

`test.bat` / `test.sh` 只检查产物是否存在并运行，不负责编译。

## 覆盖范围

- 帮助、版本与动作类参数的优先级；
- 必填项、缺值、空值、非法值与位置参数；
- `-j`：指定值、`0` 自动检测、非数字、溢出；
- `-c/-f/-C/-o/-t` 字段写入、长格式、重复参数覆盖；
- `-D`：一个 `-D` 多个键值对、重复键以后者为准、值中含 `=`；
- `--list-targets` / `--dump-config` 的动作请求字段；
- CCM 定位：单文件、链式/菱形/重复 include、循环、注释与字符串、非法指令、缺失目标、扩展名、根错误、BOM/CRLF/NUL/UTF-16、`-C`、绝对路径、空格路径、Windows 大小写去重、路径规范化与资源清理。

测试直接调用 `parse_cmd` / `ccm_locate`，不经过 `main`；命令行用例不受 `.ccm` 解析影响。

## 断言方式

`CC_CASE` / `CC_CASE_END` 包裹用例，`CC_CHECK`、`CC_CHECK_EQ_I64`、`CC_CHECK_EQ_STR`、`CC_CHECK_EQ_MEM` 断言。

解析期间的 stdout 通过**管道**（`_pipe` / `pipe` + `dup2`）临时接管后读回，并剥离终端颜色控制序列，因此可以逐字校验中文提示与行数；该方式不依赖 `TMP`/`TEMP` 或临时文件。

套件启动时先做一次 `CMD-HARNESS-000` 捕获自检：若当前环境无法接管 stdout，只报一条失败并中止，同时在 stderr 打印失败的系统调用与 errno，不会把全部用例误报为失败。

测试程序与 `src/cmd.c` 使用同一套编译参数（Windows 下都带 `-fexec-charset=GBK`），中文期望值可以直接写成字符串字面量。
