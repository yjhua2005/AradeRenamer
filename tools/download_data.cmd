@echo off
setlocal
cd /d "%~dp0"

echo ============================================
echo  正在下载 AradeRenamer 数据字典...
echo ============================================
echo.

python download_data.py

if errorlevel 1 (
    echo.
    echo [失败] 下载过程中出现错误，请检查网络或上方日志。
    pause
    exit /b 1
) else (
    echo.
    echo [成功] 所有文件已准备就绪。
)

pause
endlocal