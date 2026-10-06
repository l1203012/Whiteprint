# Releasing Whiteprint for Windows

Windows releases are separate from the macOS ones: the tag is `windows-v<version>` (macOS uses `v<version>`),
so each platform has its own GitHub Release and a failing macOS run never blocks a Windows release.

## Cut a Windows release

1. Set the version in `windows/CMakeLists.txt` (`project(Whiteprint VERSION x.y.z ...)`) and commit it.
2. Push a tag: `git tag windows-v0.1.1 && git push origin windows-v0.1.1`.
3. The `Windows Release` workflow builds, tests, packages the installer and runs
   `gh release create windows-v<version> --draft` (or uploads to it if it exists). Versions containing `-`
   are marked prerelease.
4. The draft holds `Whiteprint-<version>-Setup.exe` and `.sha256`. Review the notes, publish the draft
   (`gh release edit windows-v<version> --draft=false --latest`), then submit the winget manifest (below).

The workflow can also be run by hand from the Actions tab (`workflow_dispatch`, optional `version`).

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

## Workflows

| Workflow | Trigger | Result |
|---|---|---|
| `windows-ci.yml` | push or pull request touching `windows/` | build + `ctest` on `windows-latest` |
| `windows-edge.yml` | every push to `main-windows` | installer artifact `Whiteprint-<version>-edge.<run>`, kept 14 days, no release |
| `windows-release.yml` | `windows-v*` tag or manual run | installer + `.sha256` on the draft GitHub Release |

Edge builds are for trying a change before it is released: open the run, download the artifact (a GitHub
login is needed), unzip and run `Whiteprint-<version>-edge.<run>-Setup.exe`.

The release notes are GitHub's generated notes followed by the install section in
`.github/windows-release-notes.md`; `{{VERSION}}`, `{{SETUP}}`, `{{SHA256}}` and `{{REPOSITORY}}` are filled in by
the workflow. Edit that file to change the text.

## Publish to winget

The manifests live in `windows/installer/winget/manifests/w/Whiteprint/Whiteprint/<version>/` (package
`Whiteprint.Whiteprint`, installer type `inno`, per user). For every published release:

1. Copy the previous version's folder to `<new version>` and set `PackageVersion` in the three files.
2. In the installer manifest set `InstallerUrl` to the release asset and `InstallerSha256` to the SHA-256
   of that exact file (`Get-FileHash`, or the `.sha256` asset; use the one from the **published** release,
   since a rebuilt installer has a different hash).
3. `winget validate <folder>`.
4. Fork `microsoft/winget-pkgs`, add the folder under `manifests/w/Whiteprint/Whiteprint/`, and open a pull
   request titled `New version: Whiteprint.Whiteprint version <version>`. A bot validates the installer;
   a reviewer merges it, usually within a few days.

0.1.0 was submitted as [microsoft/winget-pkgs#447499](https://github.com/microsoft/winget-pkgs/pull/447499).
