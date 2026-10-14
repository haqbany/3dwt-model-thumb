@echo off
REM ModelThumbLite one-click installer
REM Works on any Windows 10/11. Run this .bat file (no admin needed).

cd /d "%~dp0"

echo ============================================================
echo ModelThumbLite installer
echo Adds live thumbnails + preview-pane for .stl .obj .3mf .glb .gltf .fbx .ply .las .laz
echo ============================================================
echo.

set "DLL=bin\Release\ModelThumbLite.dll"
if not exist "%DLL%" (
    echo ERROR: %DLL% not found.
    echo Place this installer next to the 'bin' folder from the release zip.
    pause
    exit /b 1
)

echo Registering DLL (per-user, no admin)...
regsvr32.exe /s "%DLL%"
if errorlevel 1 (
    echo Registration failed. Try running as Administrator once.
    pause
    exit /b 1
)

echo Clearing thumbnail cache so new thumbs generate...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe
timeout /t 3 /nobreak >nul

echo.
echo Done!
echo.
echo To see previews: open any folder with 3D files, enable 'Preview pane' (View > Show > Preview pane), then single-click a file.
echo.
pause