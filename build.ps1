# Builds ModelThumbLite (Release|x64) + test harness.
$ErrorActionPreference = "Stop"
$msbuild = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $msbuild) {
    $bp = & $msbuild -products Microsoft.VisualStudio.Product.BuildTools -latest -property installationPath
    $msb = Join-Path $bp "MSBuild\Current\Bin\MSBuild.exe"
} else {
    $msb = "MSBuild.exe"
}
if (-not (Test-Path $msb)) { $msb = "MSBuild.exe" }
Write-Host "Using: $msb"
& $msb "$PSScriptRoot\src\ModelThumbLite.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /v:m
if ($LASTEXITCODE) { throw "DLL build failed" }
& $msb "$PSScriptRoot\test\TestThumb.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /v:m
if ($LASTEXITCODE) { throw "test build failed" }
Write-Host "Built: bin\Release\ModelThumbLite.dll + test_thumb.exe"
