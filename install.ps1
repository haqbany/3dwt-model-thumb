# Registers ModelThumbLite live thumbnails (per-user, no admin).
$ErrorActionPreference = "Stop"
$dll = Join-Path $PSScriptRoot "bin\Release\ModelThumbLite.dll"
if (-not (Test-Path $dll)) { throw "Build first: run build.ps1 ($dll missing)" }
Write-Host "Registering $dll ..."
& regsvr32.exe /s $dll
if ($LASTEXITCODE) { throw "regsvr32 failed ($LASTEXITCODE)" }
# verify our CLSID resolves to this DLL
$clsid = "{E8A4B2C1-5D3F-4A7E-9C1B-6F2A4D8C0E13}"
$reg = Get-ItemProperty -Path "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\$clsid\InprocServer32" -ErrorAction Stop
Write-Host "CLSID -> $($reg.'(default)')"
Write-Host "Registered. Clearing thumbnail cache so new thumbs generate..."
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep 2
Get-ChildItem "$env:LocalAppData\Microsoft\Windows\Explorer\thumbcache_*.db" -Force -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
Start-Process explorer.exe
Write-Host "Done. Open a folder with 3D files in Large icons view."
