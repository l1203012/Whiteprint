# Whiteprint

A lightweight macOS note-taking app with blueprint-style pages: blue background, white text.
Claude connects through MCP to write notes, draw simple diagrams in a compact drawing language,
and turn course material (PDF, Word, PowerPoint) into a study plan, using your own Claude subscription.

Beta 1 is macOS only (13 Ventura or later). See [docs/BETA_PLAN.md](docs/BETA_PLAN.md) for the plan
and [docs/DSL.md](docs/DSL.md) for the drawing language.

## Build

Only the Xcode Command Line Tools are needed.

```sh
Scripts/build.sh app                 # .build/Whiteprint.app (this Mac's architecture)
CONFIG=release Scripts/build.sh dmg  # universal .build/Whiteprint.dmg
Scripts/test.sh                      # every module's tests
```

With Xcode installed, `swift build` and `swift test` work too.

## Modules

| Module | What it does |
|---|---|
| `WhiteprintCore` | `.wprint` format, note editing, Markdown export, drawing language compiler, study plan model |
| `WhiteprintExtract` | Local text extraction from PDF (with OCR), DOCX/DOC and PPTX, and chunking |
| `WhiteprintRender` | Drawing renderer, Markdown styling, PDF export, page thumbnails |
| `WhiteprintEditor` | The Notion-style page editor |
| `WhiteprintBridge` | MCP server and the Unix socket between `whiteprint-mcp` and the app |
| `WhiteprintStudy` | Imports, saved points, study plan notes, running Claude Code headlessly |
| `WhiteprintApp` | The app: documents, sidebar, command palette, settings, study panel |
| `whiteprint-mcp` | Tiny stdio helper that Claude Code / Claude Desktop starts |

## Connect Claude

Open Whiteprint → Settings → Claude, then either click **Add Whiteprint to Claude Code** or
**Connect Claude Desktop**. Or run it yourself:

```sh
claude mcp add --scope user whiteprint -- /Applications/Whiteprint.app/Contents/MacOS/whiteprint-mcp
```

## Debugging

- `WHITEPRINT_NOTES_DIR=/tmp/notes` uses a different notes folder.
- `WHITEPRINT_SOCKET=/tmp/wp.sock` uses a different socket, for the app and `whiteprint-mcp`.
