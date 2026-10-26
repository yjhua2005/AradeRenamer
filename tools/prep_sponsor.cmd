@echo off
setlocal
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0prep_sponsor.ps1" %*
if errorlevel 1 (
    echo.
    echo [失败] 处理过程中出现错误，见上方报错信息。
    pause
    exit /b 1
)
echo.
echo [完成] 收款码处理成功！
pause