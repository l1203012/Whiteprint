# Whiteprint

A lightweight Windows note-taking app with blueprint-style pages: blue background, white text.
Claude connects through MCP to write notes, draw simple diagrams in a compact drawing language,
and turn course material (PDF, Word, PowerPoint) into a study plan, using your own Claude subscription.

This is the Windows 10/11 version (C++ / Qt 6). A macOS version lives on the
[`main-macos`](https://github.com/l1203012/Whiteprint/tree/main-macos) branch, and `.wprint` notes move
between the two unchanged. See [docs/BETA_PLAN.md](docs/BETA_PLAN.md) for the plan,
[docs/DSL.md](docs/DSL.md) for the drawing language and [docs/WINDOWS_PORT.md](docs/WINDOWS_PORT.md)
for how the port is laid out.

## Install

Download `Whiteprint-<version>-Setup.exe` from the
[latest release](https://github.com/l1203012/Whiteprint/releases/latest) and run it. It installs per user
(no admin needed) and is unsigned, so SmartScreen may warn: click **More info**, then **Run anyway**.

```powershell
winget install Whiteprint.Whiteprint    # once the winget-pkgs submission is merged
```

Notes are kept in `Documents\Whiteprint`.

## Build

Needs Qt 6.8 (MinGW, in `C:\Qt`; see `windows/env.ps1`), CMake and Ninja.

```powershell
powershell -ExecutionPolicy Bypass -File windows/build.ps1                        # build + every module's tests
powershell -ExecutionPolicy Bypass -File windows/installer/package.ps1 -Version 0.1.0   # .build/windows-release/Whiteprint-0.1.0-Setup.exe
```

The installer is compiled with Inno Setup 6 (`winget install JRSoftware.InnoSetup`). Releases are built by
GitHub Actions when a `v*` tag is pushed; see [docs/RELEASING_WINDOWS.md](docs/RELEASING_WINDOWS.md).

## Modules

Everything is under `windows/`, one static library per directory.

| Module | What it does |
|---|---|
| `core` | `.wprint` format, note editing, Markdown export, drawing language compiler, study plan model |
| `extract` | Local text extraction from PDF (with Windows OCR), DOCX/DOC and PPTX, and chunking |
| `render` | Drawing renderer, Markdown styling, PDF export, page thumbnails |
| `editor` | The Notion-style page editor |
| `bridge` | MCP server and the named pipe between `whiteprint-mcp` and the app |
| `study` | Imports, saved points, study plan notes, running Claude Code headlessly |
| `app` | The app: documents, sidebar, command palette, settings, flashcards, study panel |
| `mcp` | Tiny stdio helper (`whiteprint-mcp.exe`) that Claude Code / Claude Desktop starts |

## Connect Claude

Open Whiteprint → Settings → Claude, then either click **Add Whiteprint to Claude Code** or
**Connect Claude Desktop**. Or run it yourself:

```powershell
claude mcp add --scope user whiteprint -- "$env:LOCALAPPDATA\Programs\Whiteprint\whiteprint-mcp.exe"
```

## Debugging

- `WHITEPRINT_NOTES_DIR=C:\temp\notes` uses a different notes folder.
- `WHITEPRINT_SOCKET=wp-test` uses a different named pipe (or a full `\.\pipe\...` path), for the app and
  `whiteprint-mcp`.
