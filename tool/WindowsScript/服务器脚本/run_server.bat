@echo off
REM ========== 可配置参数==========
REM 1）server.exe 与 server_log.txt 所在目录的绝对路径
set "SERVER_DIR=C:\Users\Administrator\Desktop\QtProject\Chat\server\server\release"

REM 2）Qt 的 bin 目录。打包后 DLL 在 exe 同目录时可留空（留空时自动用上面目录作为 PATH）
set "QT_BIN="
REM ==========================================

chcp 65001 >nul
cd /d "%~dp0"

set "EXE=%SERVER_DIR%\server.exe"
set "LOG=%SERVER_DIR%\server_log.txt"

if not exist "%EXE%" (
    echo 未找到 "%EXE%"，请修改脚本顶部的 SERVER_DIR
    pause
    exit /b 1
)

if defined QT_BIN (
    if exist "%QT_BIN%\Qt6Core.dll" (
        set "PATH=%QT_BIN%;%PATH%"
    ) else (
        echo 未找到 Qt，请修改 QT_BIN 或留空以使用 exe 同目录 DLL
        pause
        exit /b 1
    )
) else (
    set "PATH=%SERVER_DIR%;%PATH%"
)

taskkill /IM server.exe /F >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo 已结束之前的 server 进程，等待端口释放...
    timeout /t 2 /nobreak >nul
)

set "SKIP=0"
if exist "%LOG%" for /f "delims=" %%N in ('powershell -NoProfile -Command "(Get-Content '%LOG%' -Encoding UTF8 -ErrorAction 0).Count"') do set "SKIP=%%N"

start /B "" "%EXE%"
timeout /t 2 /nobreak >nul
if not exist "%LOG%" (
    echo 等待日志文件生成: %LOG%
    timeout /t 3 /nobreak >nul
)

echo ========== 如需在无界面模式关闭服务器 请调用脚本 ==========
echo.
powershell -NoProfile -Command "$log='%LOG%'; $skip=%SKIP%; Get-Content $log -Encoding UTF8 -Wait | Select-Object -Skip $skip"
echo.
pause
