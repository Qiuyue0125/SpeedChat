@echo off
chcp 65001 >nul
REM 强制结束所有 server.exe 进程（释放端口）
taskkill /IM server.exe /F
if %ERRORLEVEL% equ 0 (
    echo 已结束 server 进程。
) else (
    echo 当前没有运行中的 server 进程。
)
pause
