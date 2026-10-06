# Regenerates the README screenshots in docs/images/windows (the Windows twin of Scripts/make-screenshots.sh).
#   powershell -ExecutionPolicy Bypass -File windows/make-screenshots.ps1 [-NoBuild]
# Builds the screenshot tour (app/tests/ScreenshotTour.cpp), runs it offscreen on demo notes in a temporary
# folder (own defaults suite, bridge pipe and %APPDATA%, so your notes, preferences, Credential Manager and
# Claude config are never touched), then shrinks the PNGs into docs/images/windows. Needs Pillow for the last step.
param([switch]$NoBuild)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$build = "$root\.build\windows"
$raw = Join-Path ([IO.Path]::GetTempPath()) "wp-shots-raw"

if (-not $NoBuild) {
    cmake -S "$root\windows" -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    cmake --build $build --target wp_test_app_ScreenshotTour
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

if (Test-Path $raw) { Remove-Item -Recurse -Force $raw }
$env:WHITEPRINT_SCREENSHOTS = $raw
& "$build\wp_test_app_ScreenshotTour.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Remove-Item Env:WHITEPRINT_SCREENSHOTS

python "$PSScriptRoot\optimize-images.py" $raw "$root\docs\images\windows"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "docs/images/windows done"
