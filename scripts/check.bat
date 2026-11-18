@echo off
:: Please save as GBK

setlocal

echo [检测] 正在检测工具链...

:: 1) gcc 是否存在
where gcc >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到编译器 gcc，请先安装 MinGW-w64 等并将其加入 PATH。
    exit /b 1
)

:: 2) 打印版本（仅取首行）
set "gccver="
for /f "delims=" %%i in ('gcc --version 2^>nul') do if not defined gccver set "gccver=%%i"
echo [检测] gcc 版本：%gccver%

echo [检测] 工具链检测通过。
exit /b 0
