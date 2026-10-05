# Releasing Whiteprint for Windows

The Windows installer ships on the same GitHub Release as the macOS DMG (see `docs/RELEASING.md`).

## Cut a release with both artifacts

1. Make sure the version you want is on the commit you tag (the macOS workflow reads `Resources/Info.plist`
   for manual runs; the Windows project version lives in `windows/CMakeLists.txt`).
2. Push a tag: `git tag v0.1.0-beta.2 && git push origin v0.1.0-beta.2`.
3. Two workflows start: `Release` (macOS DMG) and `Windows Release` (installer). Each builds and tests, then
   runs `gh release create v<version> --draft` if the release does not exist yet, otherwise
   `gh release upload --clobber`. If both race to create it, the loser falls back to upload. Versions
   containing `-` are marked prerelease.
4. When both jobs are green, the draft `v<version>` holds `Whiteprint-<version>.dmg(.sha256)` and
   `Whiteprint-<version>-Setup.exe(.sha256)`. Review the notes and publish the draft.

Either workflow can also be run by hand from the Actions tab (`workflow_dispatch`, optional `version`).

The installer is unsigned, so SmartScreen warns on first run ("More info" then "Run anyway"). Code signing
is not wired up yet.

## Build the installer locally

Prerequisites: Qt 6.8.3 MinGW (`C:\Qt`, see `windows/env.ps1`), CMake, Ninja, and Inno Setup 6
(`winget install JRSoftware.InnoSetup`).

```powershell
powershell -ExecutionPolicy Bypass -File windows/build.ps1          # Release build + tests in .build/windows
powershell -ExecutionPolicy Bypass -File windows/installer/package.ps1 -Version 0.1.0
```

`package.ps1` runs `cmake --install` into `.build/windows-stage`, runs `windeployqt` on `Whiteprint.exe` and
`whiteprint-mcp.exe`, copies the MinGW runtime DLLs, compiles `windows/installer/Whiteprint.iss` with ISCC,
and writes `Whiteprint-<version>-Setup.exe` and `.sha256` to `.build/windows-release/`.

The installer installs per user by default (no admin; the dialog offers an all-users install), adds Start menu
and optional desktop shortcuts, and registers the `.wprint` file type with the app icon.

## Regenerating icons and wizard images

`python windows/installer/make_assets.py` (needs `pip install --user Pillow`) rebuilds `windows/app/AppIcon.ico`
from `Resources/App/AppIcon.icns` and the wizard bitmaps in `windows/installer/`.
