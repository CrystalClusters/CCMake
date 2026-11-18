@echo off
:: Please save as GBK
:: 依赖库平台识别

:: 依赖列表位于 build.bat 内 DEPENDENCIES 字段（可追加）

setlocal enabledelayedexpansion
pushd "%~dp0.." || exit /b 1

if "%~1"=="" (
    echo [错误] 用法: call depends.bat ^<platform_arch^> ^<dependencies^> 1>&2
    popd
    exit /b 2
)
if "%~2"=="" (
    echo [错误] 用法: call depends.bat ^<platform_arch^> ^<dependencies^> 1>&2
    popd
    exit /b 2
)
set "CC_PFX=%~1"
set "CC_DEPS=%~2"

if not exist "depends" (
    echo [错误] 找不到依赖目录: depends 1>&2
    popd
    exit /b 1
)

set "CC_DEP_INC="
set "CC_DEP_LIB="
set "CC_ERR_COUNT=0"

for %%N in (%CC_DEPS%) do call :one "%%~N"

:: 去掉累积时留下的前导空格
if defined CC_DEP_INC set "CC_DEP_INC=!CC_DEP_INC:~1!"
if defined CC_DEP_LIB set "CC_DEP_LIB=!CC_DEP_LIB:~1!"

if not "%CC_ERR_COUNT%"=="0" goto :fail
if not defined CC_DEP_LIB goto :no_lib

echo [依赖] 头文件参数：%CC_DEP_INC% 1>&2
echo [依赖] 链接参数：%CC_DEP_LIB% 1>&2
popd
endlocal & set "CC_DEP_INC=%CC_DEP_INC%" & set "CC_DEP_LIB=%CC_DEP_LIB%"
exit /b 0

:no_lib
echo [错误] 没有识别到任何可链接的依赖库 1>&2
popd
exit /b 1

:fail
echo [错误] 依赖识别失败，共 %CC_ERR_COUNT% 项问题，未继续构建。 1>&2
popd
exit /b 1

:: ---------------------------------------------------------------- 处理单个依赖项（名单里每个名字调用一次）

:one
set "NAME=%~1"
set "PKG="
set "CANDS="
set "MATCHES=0"

:: 忽略版本号匹配目录：名字 或 名字-v版本
if exist "depends\%NAME%\" (
    set "PKG=depends\%NAME%"
    set "CANDS=%NAME%"
    set /a MATCHES+=1
)
for /d %%D in ("depends\%NAME%-v*") do (
    set "PKG=%%D"
    set "CANDS=!CANDS! %%~nxD"
    set /a MATCHES+=1
)

if "%MATCHES%"=="0" (
    call :err "找不到依赖 %NAME%：depends\ 下没有 %NAME% 或 %NAME%-v版本 目录"
    goto :eof
)
if %MATCHES% GTR 1 (
    call :err "依赖 %NAME% 匹配到多个目录，无法确定用哪一个：!CANDS:~1!"
    goto :eof
)

:: 头文件目录可选
if exist "%PKG%\include" set "CC_DEP_INC=%CC_DEP_INC% -I%PKG%\include"

:: 当前平台的库目录
set "LIBDIR=%PKG%\%CC_PFX%_bin"
if not exist "%LIBDIR%" (
    call :err "依赖 %PKG% 缺少当前平台（%CC_PFX%）的库目录：%LIBDIR%"
    goto :eof
)

set "LIBNUM=0"
for %%L in ("%LIBDIR%\*.a") do (
    set "CC_DEP_LIB=!CC_DEP_LIB! -Wl,--whole-archive %%~L -Wl,--no-whole-archive"
    set /a LIBNUM+=1
)
if "%LIBNUM%"=="0" (
    call :err "依赖 %PKG% 的平台目录里没有静态库（.a）：%LIBDIR%"
    goto :eof
)

echo [依赖] %PKG% 1>&2
goto :eof

:: ---------------------------------------------------------------- 错误收集

:err
set /a CC_ERR_COUNT+=1
echo [错误] %~1 1>&2
goto :eof