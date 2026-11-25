@echo off
:: Please save as GBK
setlocal enabledelayedexpansion
:: 切换到项目根目录（本脚本位于 scripts/ 下）
pushd "%~dp0.." || exit /b 1

if "%~1"=="" (
    echo [错误] 用法: call build_test.bat ^<platform_arch^> [build_flags] [dependencies]
    popd
    exit /b 2
)
set "PFX=%~1"
set "CC_BUILD_CFLAGS=%~2"
set "DEPENDENCIES=%~3"
if "%DEPENDENCIES%"=="" set "DEPENDENCIES=CrystalStd"

set "BIN_DIR=out\%PFX%_bin"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

:: 依赖识别
call "%~dp0depends.bat" "%PFX%" "%DEPENDENCIES%"
if errorlevel 1 (
    echo [错误] 依赖识别未通过，中止构建。
    popd
    exit /b 1
)

:: 收集测试源码与解析器源码
set "SRCS=tests\test.c tests\test_harness.c"
for /r tests\cmd %%s in (*.c) do set "SRCS=!SRCS! %%s"
for /r tests\mc %%s in (*.c) do set "SRCS=!SRCS! %%s"
for /r tests\mk %%s in (*.c) do set "SRCS=!SRCS! %%s"
for /r tests\vars %%s in (*.c) do set "SRCS=!SRCS! %%s"
if "!SRCS!"=="" (
    echo [错误] tests\ 下没有找到测试源文件。
    popd
    exit /b 1
)
for /r src %%s in (*.c) do (
    if /i not "%%~nxs"=="main.c" set "SRCS=!SRCS! %%s"
)

set "BIN_NAME=ccmake_test.exe"
echo 编译测试程序...
gcc !SRCS! -o "%BIN_DIR%\%BIN_NAME%" -Itests -Iinclude -Isrc !CC_DEP_INC! %CC_BUILD_CFLAGS% !CC_DEP_LIB! -lws2_32 -liphlpapi
if errorlevel 1 (
    echo [错误] 编译测试程序失败。
    popd
    exit /b 1
)
echo 已生成测试程序：%BIN_DIR%\%BIN_NAME%
popd
exit /b 0
