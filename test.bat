@echo off
:: Please save as GBK
setlocal

:: CCMake 测试运行入口（Windows）
:: 测试程序由 build.bat 生成；本脚本只负责运行。
:: 用法：test.bat [all|<套件名>] [--help]

pushd "%~dp0" || exit /b 1

for /f "delims=" %%i in ('call "scripts\detect.bat"') do set "PLATFORM_STR=%%i"
set "TEST_EXE=out\%PLATFORM_STR%_bin\ccmake_test.exe"

if not exist "%TEST_EXE%" (
    echo [错误] 找不到测试程序：%TEST_EXE%
    echo [错误] 请先运行 build.bat。
    popd
    exit /b 2
)

"%TEST_EXE%" %*
set "TEST_CODE=%errorlevel%"
popd
exit /b %TEST_CODE%
