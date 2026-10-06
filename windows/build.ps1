# Configure + build + test:  powershell windows/build.ps1 [-NoTest] [-Config Release]
param([switch]$NoTest, [string]$Config = "Release")
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"
$build = "$PSScriptRoot\..\.build\windows"
cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $NoTest) { ctest --test-dir $build --output-on-failure; exit $LASTEXITCODE }
