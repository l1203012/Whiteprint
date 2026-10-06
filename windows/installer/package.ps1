# Builds Whiteprint-<version>-Setup.exe (+ .sha256) into .build/windows-release/.
#   powershell -ExecutionPolicy Bypass -File windows/installer/package.ps1 -Version 0.1.0 [-BuildDir .build/windows]
# Needs a finished Release build (windows/build.ps1), windeployqt (Qt bin on PATH) and Inno Setup 6.
param(
    [string]$Version = "0.1.0",
    [string]$BuildDir = "",
    [string]$StageDir = ""
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $BuildDir) { $BuildDir = "$root\.build\windows" }
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not $StageDir) { $StageDir = "$root\.build\windows-stage" }
$out = "$root\.build\windows-release"
$Version = $Version -replace '^v', ''

if (Test-Path "$PSScriptRoot\..\env.ps1") { . "$PSScriptRoot\..\env.ps1" }

if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force $StageDir, $out | Out-Null

cmake --install $BuildDir --prefix $StageDir
if ($LASTEXITCODE) { throw "cmake --install failed" }

$deploy = (Get-Command windeployqt -ErrorAction Stop).Source
& $deploy --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$StageDir\Whiteprint.exe" "$StageDir\whiteprint-mcp.exe"
if ($LASTEXITCODE) { throw "windeployqt failed" }

# MinGW runtime DLLs next to the exe (windeployqt copies them when it finds the toolchain; make sure).
$gcc = Get-Command g++ -ErrorAction SilentlyContinue
if ($gcc) {
    $bin = Split-Path $gcc.Source
    foreach ($dll in "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll") {
        if ((Test-Path "$bin\$dll") -and -not (Test-Path "$StageDir\$dll")) { Copy-Item "$bin\$dll" $StageDir }
    }
}

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") {
        if (Test-Path $p) { $iscc = $p; break }
    }
}
if (-not $iscc) { throw "ISCC.exe (Inno Setup 6) not found" }

& $iscc "/DAppVersion=$Version" "/DStageDir=$StageDir" "/DOutDir=$out" "$PSScriptRoot\Whiteprint.iss"
if ($LASTEXITCODE) { throw "ISCC failed" }

$setup = "$out\Whiteprint-$Version-Setup.exe"
$hash = ([System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash([System.IO.File]::ReadAllBytes($setup))) -replace "-","").ToLower()
# Same format as shasum output on macOS: "<hash>  <file>".
[IO.File]::WriteAllText("$setup.sha256", "$hash  Whiteprint-$Version-Setup.exe`n")
Write-Host "Wrote $setup"
