# Windows port (C++ / Qt 6)

Branch `windows-cpp`. Everything lives under `windows/`; the macOS sources (`Sources/`, `Tests/`,
`Package.swift`, `Scripts/`, `Resources/`, the macOS workflows) are the reference and are not modified.

## Stack
C++20, Qt 6.8 Widgets (LGPL, dynamic), CMake + Ninja, MinGW-w64 13 locally and in CI, Inno Setup for
the installer. Qt types (`QString`, `QJsonObject`, `QPainter`, `QProcess`, `QLocalSocket`) replace
Foundation/AppKit/CoreGraphics. Namespace `wp`.

## Modules (mirror the Swift targets; dependencies as in Package.swift)
| Dir | Swift target | Qt parts |
|---|---|---|
| `core` | WhiteprintCore | Core |
| `extract` | WhiteprintExtract | Core, Gui, Pdf |
| `render` | WhiteprintRender | Gui, Widgets, PrintSupport |
| `bridge` | WhiteprintBridge | Network (QLocalServer/QLocalSocket = named pipe instead of Unix socket) |
| `study` | WhiteprintStudy | Network, Core (QProcess) |
| `editor` | WhiteprintEditor | Widgets |
| `app` | WhiteprintApp | Widgets |
| `mcp` | whiteprint-mcp | stdio helper |

Headers are included from the `windows/` root: `#include "core/Note.h"`. Each module is a static
library `wp_<dir>` globbed from `<dir>/*.h|*.cpp`; `<dir>/tests/*.cpp` become Qt Test executables
(ports of `Tests/<Target>Tests`). `app/tests/` tests link the app logic (everything but `main.cpp`).

## Conventions
- Port faithfully: same type/function names (C++ casing for functions: `camelCase`, types `PascalCase`),
  same behaviour, same on-disk formats (`.wprint` files, notes folder layout, MCP tool names/JSON) so
  notes move between macOS and Windows unchanged.
- Dark "blueprint" look and macOS-style chrome: see the Swift `Palette`, `BlueprintBackground`, and the
  app/sidebar/palette/settings windows for colours, spacing, fonts and behaviour.
- Windows specifics: notes dir `%USERPROFILE%\Documents\Whiteprint` equivalent of the macOS default
  (check `NotesFolder.swift`), pipe `\.\pipe\whiteprint-<user>` for the bridge, API keys via Windows
  Credential Manager (`CredRead/CredWrite`), Ctrl instead of Cmd.
- Build/test: `powershell -ExecutionPolicy Bypass -File windows/build.ps1` (uses `C:\Qt` from `env.ps1`).
- Warnings are not errors, but keep the build warning-free for your module.
