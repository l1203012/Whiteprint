# Whiteprint — Beta 1 Plan (macOS only)

A lightweight, native macOS note-taking app with a blueprint look (blue pages, white text).
Claude connects through MCP to write notes, draw simple diagrams cheaply, and turn course
material (PDF / Word / PowerPoint) into a study plan. **All AI features run on your existing
Claude subscription. No API key is needed.**

---

## 0. Toolchain constraints

Current machine: macOS 13.7 (Intel), Swift 5.8 / Xcode 14.3.

- **Deployment target: macOS 13.** No SwiftData or `@Observable` (they need macOS 14). Use `ObservableObject` and plain files.
- **Upgrade to Xcode 15.2** (the newest that runs on Ventura) for Swift 5.9, still targeting macOS 13.
- **The official MCP Swift SDK needs Swift 6**, so we write a small JSON-RPC 2.0 stdio handler ourselves (about 300 lines: `initialize`, `tools/list`, `tools/call`, `resources/*`, `prompts/*`).

## 1. Architecture

```
Whiteprint.app (AppKit + SwiftUI, NSDocument-based)
├── WhiteprintCore    pure Swift package, no AppKit: file format, DSL parser, scene model,
│                     MD export, document extraction, study-plan model
├── WhiteprintUI      AppKit/SwiftUI: editor, blueprint renderer, PDF export, import UI
└── Contents/MacOS/whiteprint-mcp     tiny stdio MCP server (spawned by Claude Desktop / Claude Code)
        │  Unix domain socket: ~/Library/Application Support/Whiteprint/mcp.sock
        ▼
     running app = single source of truth (Claude's edits appear live)
```

- **NSDocument** gives autosave, Versions, Recent Files and dirty-state handling for free.
- **WhiteprintCore has no AppKit**, so it can be reused for a later Windows 11 version.
- **If the app isn't running**, the MCP helper starts it with `open -b`.

## 2. RAM budget

Target: **under 60 MB idle, under 120 MB with a 50-page note open, MCP helper under 10 MB.**

- No WebKit or Electron. Text uses `NSTextView` (TextKit 2). Drawings are vector Core Graphics.
- No bitmap caches for drawings; they are redrawn on demand.
- Pages are laid out lazily; only visible pages get views.
- Document extraction streams one page or slide at a time inside `autoreleasepool`.
- Zero third-party dependencies except ZIPFoundation (for PPTX) and optionally Sparkle.
- An Instruments Allocations + Leaks pass is required before release.

## 3. File format: `.wprint`

Plain text: Markdown plus fenced drawing blocks. Custom UTType `com.whiteprint.note`.

````text
---
whiteprint: 1
title: Network sketch
---
# Overview
Some notes in **markdown**.

```wp
box c 0,0 "Client"
box s 12,0 "Server"
db  d 24,0 "Postgres"
arrow c>s "HTTPS"
arrow s>d
```

+++page
# Page 2
````

- **Export to MD:** strip the `wp` blocks (optionally leave `*[drawing omitted]*`) and the front matter.
- **Export to PDF:** typeset the text with page breaks and no drawings. Two styles: Blueprint and Print (white paper).

## 4. Drawing DSL (token-efficient)

- Grid units (1 unit = 10 pt), short ids, defaults for size, font and stroke. Arrows attach to shapes by id.
- Auto-layout: `flow a>b>c`, `row a b c`, `col a b c`, so coordinates are often not needed.
- Primitives: `box`, `circle`, `db`, `text`, `line`, `arrow`, `path`, `dim` (dimension line), `group`.
- Errors come back as one line, e.g. `line 3: unknown id 'x'`.
- The full grammar lives in one MCP resource (`whiteprint://dsl`), not in tool descriptions.

## 5. MCP surface

All responses are minimal (e.g. `ok d3`). The document is never echoed back.

**Note tools**

| Tool | Purpose | Returns |
|---|---|---|
| `list_notes` | Open and recent notes | `id title pages`, one line each |
| `read_note(id, page?)` | Raw `.wprint` text | text |
| `create_note(title, md?)` | New document | id |
| `write(id, page, md, mode=append\|replace)` | Text edits | `ok` |
| `draw(id, page, dsl, after?)` | Insert a drawing | drawing id |
| `edit_drawing(id, dsl)` / `delete_drawing(id)` | Modify a drawing | `ok` |
| `add_page(id)` | New page | page number |

**Study tools** (see section 9)

| Tool | Purpose | Returns |
|---|---|---|
| `list_imports` | Imported documents | `id name pages chunks status` |
| `read_chunk(import, n)` | One cleaned chunk (~6k tokens), lines tagged with source refs | text |
| `save_points(import, n, points[])` | Store per-chunk findings in the app | `ok` |
| `get_points(import?)` | Compact list of every saved point | text |
| `build_study_plan(imports[], plan)` | Structured plan → new `.wprint` note | note id |

**Prompts and resources**

- MCP prompt `study_plan`: one-click instructions for building a study plan (shows up in Claude Desktop's prompt menu).
- Resource `whiteprint://dsl`: the drawing grammar.

**Setup:** Settings → "Connect to Claude" adds the helper to
`~/Library/Application Support/Claude/claude_desktop_config.json` after you confirm, and shows the
`claude mcp add whiteprint -- /Applications/Whiteprint.app/Contents/MacOS/whiteprint-mcp` command for Claude Code.

## 6. Beta 1 UI: Notion-style neatness, blueprint pages

The aim is Notion's calm, uncluttered layout, but **every page keeps the blue blueprint background
with white text**. That is a hard requirement and does not depend on light or dark mode.

**Window layout**

```
┌──────────────┬─────────────────────────────────────────────┐
│ ⌕ Search  ⌘K │  ‹ Notes / Networking / Overview      ⋯     │  ← slim breadcrumb bar
│              │                                             │
│ ▾ Notes      │     ┌───────────────────────────────────┐   │
│   Overview   │     │  # Overview         (blue page,   │   │
│   Lecture 3  │     │                      white text,  │   │
│ ▾ Study      │     │  Text, lists, - [ ] todos         │   │
│   Exam plan  │     │  ┌ drawing ┐   faint grid)        │   │
│              │     │  └─────────┘                      │   │
│ + New note   │     └───────────────────────────────────┘   │
└──────────────┴─────────────────────────────────────────────┘
```

- **Chrome stays neutral; only the pages are blue.** Sidebar and toolbar use the system
  appearance (light/dark), so the blue pages stand out like sheets on a desk.
- **Collapsible sidebar** (⌘\\) with a note tree, a "Study" section, and a "+ New note" button.
  Native `NSSplitViewController` with a source-list style.
- **Quiet toolbar:** breadcrumb plus a `⋯` menu (export, page info). No row of formatting buttons.
- **Centered page column** (max ~720 pt of text width) with generous margins and plenty of spacing between lines.
- **Typography:** SF Pro for text, SF Mono for code and the optional "technical" look. Clear
  heading sizes (H1/H2/H3) and nothing decorative.
- **Slash menu (`/`)** to insert a heading, list, checklist, code block, divider, drawing or new page,
  in the Notion style. **Markdown shortcuts** too (`# `, `- `, `[] `, ```` ``` ````).
- **Hover handles** (`⋮⋮`) beside blocks to drag them or open a block menu, shown only on hover.
- **Command palette (⌘K)** for jumping between notes, running commands and starting "Generate study plan".
- **Checklists** render as real checkboxes and can be toggled with a click.
- **Drawings** sit inline as clean white-line vector blocks; double-click one to edit its DSL in a popover.
- **Study panel:** a sheet with a drop zone, list of imported files, a "Generate study plan" button and live progress from Claude.
- **Animations:** short and subtle (sidebar slide, menu fade). Respect "Reduce motion".

**Page palette** (fixed in both light and dark mode)

| Token | Value | Use |
|---|---|---|
| `page.bg` | `#1E4D8C` | page background |
| `page.grid` | white at 6% | faint grid |
| `page.text` | `#FFFFFF` | body text |
| `page.muted` | white at 65% | secondary text, placeholders |
| `page.accent` | `#9FD3FF` | links, selection, checked boxes |

**Not in Beta 1:** freehand drawing, iCloud sync, images, databases/tables, Windows.

## 7. Milestones

| # | Deliverable | Rough time |
|---|---|---|
| M1 | Core: `.wprint` parser/serializer, DSL parser, scene model, tests | 1 week |
| M2 | Document app: NSDocument, UTType, blueprint pages, text editing, pages | 1.5 weeks |
| M3 | Renderer: DSL → Core Graphics, inline attachments, DSL popover | 1.5 weeks |
| M4 | MCP helper, socket bridge, "Connect to Claude" | 1 week |
| M5 | MD and PDF export | 3 days |
| M8 | Extraction (PDF, OCR, DOCX, PPTX), cleaning, chunker with source refs | 1 week |
| M9 | Study tools + `study_plan` prompt, plan renderer, study panel, Claude Code runner | 1.5 weeks |
| M6 | Memory and performance pass, crash fixes | 3 days |
| M7 | Developer ID signing, notarization, DMG, optional Sparkle | 2 days |

**About 9 weeks in total.** Start with a throwaway spike in the first days: DSL → render → MCP `draw`.

## 8. Decisions

- Distribute outside the Mac App Store for the beta (Developer ID + notarized DMG). The sandbox complicates the helper and socket setup.
- Drawings are inline blocks, not a free canvas.
- Drawings are stored as text in the file, so Claude can read and edit them cheaply.
- **No API key in Beta 1.** All AI runs through the user's Claude subscription via MCP.

---

## 9. Study agent: course material → study plan

### Output (a new Whiteprint note)

- **Overview:** what the material covers, in 5–10 lines.
- **Learning path:** ordered modules with estimated study time.
- **Importance tiers** for every concept:
  - ★ **Must know:** core concepts, likely exam material.
  - ○ **Good to know:** supporting detail.
  - ✕ **Can skip:** examples, history, repeated content, filler slides.
- **To-do checklist** (`- [ ]`): assignments, exercises, deadlines, readings.
- **Source references** on every point: `Lecture3.pptx · slide 14`, `Syllabus.pdf · p. 6`.
- **Optional diagrams** in the Whiteprint DSL (concept maps, process flows).

### Using your Claude subscription

The AI work is done by Claude Desktop or Claude Code, signed in with your own Pro/Max account.
Whiteprint never handles your Claude login and never calls the API itself. It only exposes MCP tools.

**Mode A: one-click button via Claude Code (default)**

1. Drop the files into Whiteprint's study panel. Extraction runs locally, with no tokens used.
2. Click **"Generate study plan"** (or use ⌘K).
3. Whiteprint runs your locally installed, logged-in Claude Code in the background:

```sh
claude -p "<study_plan prompt>" \
  --mcp-config ~/Library/Application\ Support/Whiteprint/mcp.json \
  --allowedTools "mcp__whiteprint__*" \
  --output-format stream-json
```

4. Claude walks the chunks (`read_chunk` → `save_points`), then merges them (`get_points` → `build_study_plan`).
5. Progress streams into the study panel and the finished note opens automatically. Usage counts against your subscription.

- If `claude` isn't found on `PATH`, the button explains how to install Claude Code and log in.
- ⚠️ This is fine for personal use with your own login. Before giving the beta to others, check
  Anthropic's current terms on third-party apps driving Claude Code with consumer subscriptions.
  If in doubt, give other testers only Mode B.

**Mode B: Claude Desktop (fallback)**

In Claude Desktop, pick the **`study_plan`** prompt (or say "make a study plan from my Whiteprint
imports"). Claude uses the same tools, and the note appears live in Whiteprint.

**Mode C: API key (optional, after the beta).** Off by default. It would be the same pipeline with a Keychain-stored key.

### Why the state lives in Whiteprint, not in Claude's context

Claude saves its findings for each chunk with `save_points`, then does the final merge from the
compact `get_points` list. Large course packs (hundreds of slides) therefore don't overflow a single
conversation, and an interrupted run can resume where it stopped. It also keeps subscription usage low.

### Local extraction (free, private)

| Format | Method | Dependency |
|---|---|---|
| PDF | PDFKit, page by page | built in |
| Scanned PDF | Vision OCR (`VNRecognizeTextRequest`) on pages with no text | built in |
| DOCX / DOC | `NSAttributedString(url:)` with `.officeOpenXML` / `.docFormat` | built in |
| PPTX | unzip, read `ppt/slides/slideN.xml` and speaker notes | ZIPFoundation |

Cleaning before chunking: repeated headers, footers and slide numbers are removed. Headings, slide
titles and bold text are kept as importance hints. Results are cached per file hash.

### Privacy

Only extracted text is shared, and only with your own Claude client when it calls the tools.
Raw files never leave the Mac. The extraction cache can be cleared in Settings.

---

## Open questions

1. Diagrams only, or also technical sketches (which would raise the priority of `dim`, arcs and angles)?
2. Should PDF export default to Blueprint or Print?
3. Do you have an Apple Developer account for notarization?
4. Is the study agent for courses and exams only, or also work documents?
5. Should it also make a day-by-day study schedule when it finds deadlines?
6. Will Beta 1 testers other than you get Mode A (see the warning above)?
