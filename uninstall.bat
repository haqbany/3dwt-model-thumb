@echo off
REM ModelThumbLite one-click uninstaller
REM Removes live thumbnails + preview-pane.

cd /d "%~dp0"

echo ============================================================
echo ModelThumbLite uninstaller
echo ============================================================
echo.

set "DLL=bin\Release\ModelThumbLite.dll"
if exist "%DLL%" (
    echo Unregistering...
    regsvr32.exe /s /u "%DLL%"
)

echo Clearing thumbnail cache...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe
timeout /t 3 /nobreak >nul

echo Done. Built-in Windows thumbnails will handle .stl/.obj/.3mf again.
echo.
pause