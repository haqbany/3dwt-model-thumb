# Unregisters ModelThumbLite and restores the previous state.
$ErrorActionPreference = "Continue"
$dll = Join-Path $PSScriptRoot "bin\Release\ModelThumbLite.dll"
if (Test-Path $dll) { & regsvr32.exe /s /u $dll }
Write-Host "Unregistered (your .stl/.obj/.3mf fall back to the built-in Windows provider)."
