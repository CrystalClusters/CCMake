@echo off
:: Please save as GBK
:: 编译并链接出可执行文件（单二进制）
::
:: 用法: call scripts\build_bin.bat <platform_arch> [build_flags] [dependencies]
::
:: 依赖项名称由 build.bat 的 DEPENDENCIES 传入，再交给 scripts\depends.bat 解析，
:: 结果在 CC_DEP_INC / CC_DEP_LIB 中。

setlocal enabledelayedexpansion
:: 切换到项目根目录（本脚本位于 scripts\ 下）
pushd "%~dp0.." || exit /b 1

if "%~1"=="" (
    echo [错误] 用法: call build_bin.bat ^<platform_arch^> [build_flags] [dependencies]
    popd
    exit /b 2
)
set "PFX=%~1"
set "CC_BUILD_CFLAGS=%~2"
set "DEPENDENCIES=%~3"

if "%DEPENDENCIES%"=="" (
    echo [错误] 依赖项列表为空，请在 build.bat 的 DEPENDENCIES 中填写。
    popd
    exit /b 1
)

set "OBJ_DIR=out\obj"
set "BIN_DIR=out\%PFX%_bin"
set "BIN_NAME=ccmake.exe"
if not "x%PFX:amd64=%"=="x%PFX%" set "CC_BUILD_CFLAGS=%CC_BUILD_CFLAGS% -DCC_ARCH_AMD64"
if not "x%PFX:arm64=%"=="x%PFX%" set "CC_BUILD_CFLAGS=%CC_BUILD_CFLAGS% -DCC_ARCH_ARM64"
if not "x%PFX:x86=%"=="x%PFX%" set "CC_BUILD_CFLAGS=%CC_BUILD_CFLAGS% -DCC_ARCH_X86"
if not "x%PFX:arm=%"=="x%PFX%" set "CC_BUILD_CFLAGS=%CC_BUILD_CFLAGS% -DCC_ARCH_ARM"

:: 创建输出目录
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

:: 依赖库平台识别
call "%~dp0depends.bat" "%PFX%" "%DEPENDENCIES%"
if errorlevel 1 (
    echo [错误] 依赖识别未通过，中止构建。
    popd
    exit /b 1
)

:: 逐个编译obj
set "OBJ_LIST="
set "SRC_COUNT=0"
for /r src %%s in (*.c) do (
    set "name=%%s"
    set "name=!name:%CD%\=!"
    set "name=!name:\=_!"
    set "name=!name:.c=.o!"
    set "obj=%OBJ_DIR%\!name!"
    echo 编译 %%s -^> !obj!
    gcc -c "%%s" -o "!obj!" -Iinclude -Isrc !CC_DEP_INC! %CC_BUILD_CFLAGS%
    if errorlevel 1 (
        echo [错误] 编译失败: %%s
        popd
        exit /b 1
    )
    set "OBJ_LIST=!OBJ_LIST! !obj!"
    set /a SRC_COUNT+=1
)

if "!SRC_COUNT!"=="0" (
    echo [错误] src\ 下没有找到任何 .c 源文件，请先添加源码。
    popd
    exit /b 1
)

:: 链接
echo 链接 %BIN_DIR%\%BIN_NAME%
gcc !OBJ_LIST! -o "%BIN_DIR%\%BIN_NAME%" %CC_BUILD_CFLAGS% !CC_DEP_LIB! -lws2_32 -liphlpapi
if errorlevel 1 (
    echo [错误] 链接失败: %BIN_DIR%\%BIN_NAME%
    popd
    exit /b 1
)
echo 已生成可执行文件：%BIN_DIR%\%BIN_NAME%
popd
exit /b 0