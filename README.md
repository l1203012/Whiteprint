<div align="center">

<img src="docs/images/icon.png" width="128" height="128" alt="Whiteprint app icon">

# Whiteprint

**Blueprint-style notes for macOS and Windows, with Claude drawing your diagrams.**

[![macOS CI](https://github.com/l1203012/Whiteprint-Notetaking-Application/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/l1203012/Whiteprint-Notetaking-Application/actions/workflows/ci.yml)
[![Windows CI](https://github.com/l1203012/Whiteprint-Notetaking-Application/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/l1203012/Whiteprint-Notetaking-Application/actions/workflows/windows-ci.yml)
[![Latest release](https://img.shields.io/github/v/release/l1203012/Whiteprint-Notetaking-Application?include_prereleases&sort=semver&label=release)](https://github.com/l1203012/Whiteprint-Notetaking-Application/releases)
[![Downloads](https://img.shields.io/github/downloads/l1203012/Whiteprint-Notetaking-Application/total)](https://github.com/l1203012/Whiteprint-Notetaking-Application/releases)
<br>
[![macOS 13+](https://img.shields.io/badge/macOS-13%2B-1E4D8C?logo=apple&logoColor=white)](#macos)
[![Windows 10+](https://img.shields.io/badge/Windows-10%2B-1E4D8C?logo=windows&logoColor=white)](#windows)
[![Swift 5.8+](https://img.shields.io/badge/Swift-5.8%2B-F05138?logo=swift&logoColor=white)](#macos-1)
[![C++20 and Qt 6.8](https://img.shields.io/badge/C%2B%2B20-Qt%206.8-41CD52?logo=qt&logoColor=white)](#windows-1)
[![MIT license](https://img.shields.io/badge/license-MIT-9FD3FF)](LICENSE)

[**Download**](https://github.com/l1203012/Whiteprint-Notetaking-Application/releases) ·
[Features](#features) ·
[Claude](#connect-claude) ·
[Study](#study-plans-and-flashcards) ·
[Build](#development)

<br>

<img src="docs/images/main.png" alt="Whiteprint on macOS: a note with a heading, a request-flow diagram and a checklist on a blue blueprint page, with the folder tree in the sidebar and three tabs">

</div>

Whiteprint is a native notes app for macOS and Windows where every page is a sheet of blueprint paper:
blue background, faint grid, white text. You write in Markdown with a calm, Notion-style editor. Claude
connects over MCP to read and write your notes and to draw diagrams in a compact drawing language, so a
whole diagram costs a few dozen tokens. Drop in lecture slides, PDFs or Word files and Whiteprint turns
them into a study plan with flashcards, using your own Claude subscription (or a Grok API key).

There are two native apps from one design: a Swift/AppKit app for the Mac (`Sources/`) and a C++/Qt
port for Windows (`windows/`). Both read and write the same `.wprint` files, so notes move between them
unchanged.

> [!NOTE]
> **0.1.1** ships for macOS (Apple Silicon and Intel DMGs) and Windows (installer) in one release.
> The builds are not notarized or code-signed yet; see first launch on [macOS](#macos) and [Windows](#windows).

## Contents

- [Features](#features)
- [Install](#install): [macOS](#macos) · [Windows](#windows)
- [Using Whiteprint](#using-whiteprint)
- [Connect Claude](#connect-claude)
- [Study plans and flashcards](#study-plans-and-flashcards)
- [File format](#file-format)
- [Architecture](#architecture)
- [Development](#development)
- [CI/CD](#cicd)
- [Roadmap](#roadmap) · [Contributing](#contributing) · [License](#license)

## Features

| | |
|---|---|
| 📐 **Blueprint pages** | Blue sheets, white text, a faint grid, in light and dark mode. The window chrome follows the system; the pages always stay blue. |
| ✍️ **Notion-style editor** | `/` menu, live Markdown, real checkboxes, hover handles, multi-page notes, Slides or A4 layout, Markdown syntax shown or hidden. |
| 🔷 **Drawings as text** | Boxes, databases, arrows and auto-layout in a [one-line-per-shape language](docs/DSL.md). Double-click a drawing to edit it with a live preview. |
| 🤖 **Claude over MCP** | 15 tools for notes, drawings, imports and study plans, a `whiteprint://dsl` resource and two prompts. Edits appear live in the open note. |
| 🎓 **Study plans** | PDF, Word and PowerPoint in; a note with ★ must-know / ○ good-to-know / ✕ skip tiers, a learning path, a to-do list and flashcards out. |
| 🗂️ **Flashcards** | Decks live inside notes. Study them with spaced repetition (Again / Hard / Good / Easy). |
| 📄 **Export** | PDF on blueprint paper with a title block, PDF for printing, or plain Markdown. |
| 🗃️ **Folders** | Notes are plain `.wprint` files in `Documents/Whiteprint`, in folders as deep as you like. On macOS they open as tabs, on Windows each note gets its own window. |
| ⌨️ **Keyboard first** | <kbd>⌘</kbd><kbd>K</kbd> / <kbd>Ctrl</kbd><kbd>K</kbd> command palette, shortcuts for every format, a Touch Bar on Macs that have one. |
| 🪶 **Light** | Native AppKit on the Mac and Qt Widgets on Windows: no Electron, no web view. The Mac app is a universal binary for Intel and Apple Silicon. |

<table>
  <tr>
    <td width="50%"><img src="docs/images/slash-menu.png" alt="The slash menu open under a paragraph, listing headings, lists, checklist and quote"></td>
    <td width="50%"><img src="docs/images/command-palette.png" alt="The ⌘K command palette listing notes with their folders and the note's pages"></td>
  </tr>
  <tr>
    <td align="center"><sub>Type <code>/</code> for blocks</sub></td>
    <td align="center"><sub><kbd>⌘</kbd><kbd>K</kbd> jumps to notes, pages and commands</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/drawing-editor.png" alt="The drawing popover: drawing-language source above a live preview"></td>
    <td width="50%"><img src="docs/images/main-dark.png" alt="The same note with dark window chrome; the page stays blue"></td>
  </tr>
  <tr>
    <td align="center"><sub>Double-click a drawing to edit its source</sub></td>
    <td align="center"><sub>Dark mode: the chrome changes, the paper doesn't</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/windows/main.png" alt="Whiteprint on Windows: the same kind of note on a blueprint page, with the folder tree and the page list in the sidebar"></td>
    <td width="50%"><img src="docs/images/windows/main-dark.png" alt="Whiteprint on Windows in dark mode; the page stays blue"></td>
  </tr>
  <tr>
    <td align="center"><sub>The Windows app</sub></td>
    <td align="center"><sub>…and in dark mode</sub></td>
  </tr>
</table>

More Windows screenshots are in [`docs/images/windows`](docs/images/windows).

## Install

AI features need Claude Code, Claude Desktop or a Grok API key; everything else works without them.

### macOS

<img src="docs/images/install-dmg.png" width="540" align="right" alt="The Whiteprint disk image window: drag the Whiteprint icon onto the Applications folder">

**Requirements:** macOS 13 Ventura or later, Intel or Apple Silicon.

#### 1. Download

From [**Releases**](https://github.com/l1203012/Whiteprint-Notetaking-Application/releases), get the DMG
for your Mac and its `.sha256`:

- **Apple Silicon** (M1, M2, M3, …): `Whiteprint-<version>-AppleSilicon.dmg`
- **Intel**: `Whiteprint-<version>-Intel.dmg`

Apple menu → About This Mac shows which one you have. Each release also has the Windows installer.

#### 2. Check the download (optional)

In Terminal, in the folder you downloaded both files to:

```sh
shasum -a 256 -c Whiteprint-0.1.1-AppleSilicon.dmg.sha256
# Whiteprint-0.1.1-AppleSilicon.dmg: OK
```

#### 3. Drag to Applications

Open the DMG and drag **Whiteprint** onto **Applications**, then eject the disk image.

<br clear="right">

#### 4. First launch of an unsigned beta

Beta builds are signed ad hoc, not with a Developer ID, so the first time macOS says *"Whiteprint" cannot
be opened because Apple cannot check it for malicious software* (or *"Whiteprint" Not Opened*). Open it
once in one of these ways; later launches work normally.

- **Right-click → Open.** In Finder, Control-click Whiteprint in Applications, choose **Open**, then
  **Open** again.
- **Privacy & Security.** Open Whiteprint and dismiss the warning, then go to **System Settings → Privacy
  & Security**, scroll to *"Whiteprint" was blocked…*, click **Open Anyway** and then **Open**.
- **Last resort, Terminal.** Only for a DMG from this repository whose checksum matched:

  ```sh
  xattr -dr com.apple.quarantine /Applications/Whiteprint.app
  ```

#### 5. Your notes

Notes are saved in **`~/Documents/Whiteprint`** as plain `.wprint` files; subfolders show up as folders
in the sidebar. Pick another folder in **Settings ▸ Notes**. On first launch with an empty folder,
Whiteprint writes a short Welcome note.

<details>
<summary><b>Updating and uninstalling on macOS</b></summary>

**Update:** quit Whiteprint, download the new DMG and drag Whiteprint to Applications again, choosing
**Replace**. Your notes and settings are kept. A new unsigned beta may need the first-launch step again.

**Uninstall:** quit Whiteprint and move `/Applications/Whiteprint.app` to the Trash. To remove
everything else too:

```sh
rm -rf ~/Library/Application\ Support/Whiteprint          # study imports, flashcard progress, socket
defaults delete io.github.l1203012.whiteprint             # preferences
claude mcp remove --scope user whiteprint                 # if you added it to Claude Code
```

Remove the `whiteprint` entry from `~/Library/Application Support/Claude/claude_desktop_config.json`
if you connected Claude Desktop, and the "Whiteprint – xai-api-key" item from Keychain Access if you
saved a Grok key. Your notes in `~/Documents/Whiteprint` are yours to keep or delete.

</details>

### Windows

**Requirements:** Windows 10 or 11, 64-bit.

#### With winget

```powershell
winget install Whiteprint.Whiteprint
```

> [!NOTE]
> The package is submitted to the winget community repository
> ([microsoft/winget-pkgs#447499](https://github.com/microsoft/winget-pkgs/pull/447499)). Until it is
> merged, use the installer below.

#### 1. Download

Get `Whiteprint-<version>-Setup.exe` and `Whiteprint-<version>-Setup.exe.sha256` from
[**Releases**](https://github.com/l1203012/Whiteprint-Notetaking-Application/releases).

#### 2. Check the download (optional)

In PowerShell, in the folder you downloaded both files to:

```powershell
(Get-FileHash Whiteprint-0.1.1-Setup.exe -Algorithm SHA256).Hash -eq (Get-Content Whiteprint-0.1.1-Setup.exe.sha256).Split(' ')[0]
# True
```

#### 3. First launch of an unsigned installer

The installer is not code-signed yet, so Windows SmartScreen may say *"Windows protected your PC"*. Click
**More info**, then **Run anyway**. The installer offers an optional desktop shortcut; later launches
work normally.

- It installs **for your user only** by default, so no administrator rights are needed. The first page
  also offers an all-users install.
- It adds Start menu (and optional desktop) shortcuts and makes `.wprint` files open in Whiteprint, with
  the app icon.

#### 4. Your notes

Notes are saved in **`Documents\Whiteprint`** as plain `.wprint` files; subfolders show up as folders in
the sidebar. Pick another folder in **Settings ▸ Notes**. On first launch with an empty folder,
Whiteprint writes a short Welcome note.

<details>
<summary><b>Updating and uninstalling on Windows</b></summary>

**Update:** run the new `Setup.exe` over the old install, or `winget upgrade Whiteprint.Whiteprint`. Your
notes and settings are kept.

**Uninstall:** **Settings ▸ Apps ▸ Installed apps ▸ Whiteprint ▸ Uninstall**, or
`winget uninstall Whiteprint.Whiteprint`. To remove everything else too:

```powershell
Remove-Item -Recurse "$env:APPDATA\Whiteprint"                # study imports, flashcard progress
Remove-Item -Recurse "HKCU:\Software\Whiteprint"              # preferences
claude mcp remove --scope user whiteprint                     # if you added it to Claude Code
```

Remove the `whiteprint` entry from `%APPDATA%\Claude\claude_desktop_config.json` if you connected Claude
Desktop, and the `io.github.l1203012.whiteprint/xai-api-key` entry from **Credential Manager** if you saved a
Grok key. Your notes in `Documents\Whiteprint` are yours to keep or delete.

</details>

## Using Whiteprint

Shortcuts are written macOS-first; on Windows use <kbd>Ctrl</kbd> for <kbd>⌘</kbd>, <kbd>Alt</kbd> for
<kbd>⌥</kbd>, <kbd>Enter</kbd> for <kbd>↩</kbd>. The full table for both is [below](#keyboard-shortcuts).

### Editor

- **Write Markdown.** Headings, **bold**, _italic_, `code`, lists, quotes and code blocks are styled
  as you type. With **View ▸ Show Markdown Syntax** off (the default) the markup hides except in the
  paragraph you are editing.
- **`/` menu.** Type `/` at the start of a line or after a space for Heading 1–3, Bulleted list,
  Numbered list, Checklist, Quote, Code block, Divider, Drawing, Flashcards or New page. Keep typing to
  filter, <kbd>↩</kbd> to insert.
- **Lists and checklists.** <kbd>↩</kbd> continues a list and ends it on an empty item, <kbd>⇥</kbd> /
  <kbd>⇧</kbd><kbd>⇥</kbd> indent and outdent, and `[] ` or `[x] ` at the start of a line becomes a
  checkbox. Click a checkbox to tick it.
- **Drawings.** **Note ▸ Insert Drawing** (<kbd>⇧</kbd><kbd>⌘</kbd><kbd>D</kbd>) or `/drawing`, then
  double-click it: a popover shows the source and a live preview. <kbd>⌘</kbd><kbd>↩</kbd> applies.
- **Flashcard decks.** **Note ▸ Insert Flashcards** (<kbd>⇧</kbd><kbd>⌘</kbd><kbd>F</kbd>) adds a deck
  block. Click a card to reveal its answer, **Edit** to change cards, **Study** to start a session.
- **Pages.** <kbd>⌥</kbd><kbd>⌘</kbd><kbd>N</kbd> adds a page. The sidebar's **Pages** section shows
  thumbnails of each one.
- **Hover handles** (`⋮⋮`) beside each block open a menu to delete, duplicate or move the block up or
  down, and to edit or study drawings and decks.

<table>
  <tr>
    <td width="33%"><img src="docs/images/main.png" alt="Slides layout"></td>
    <td width="33%"><img src="docs/images/a4-layout.png" alt="A4 layout"></td>
    <td width="33%"><img src="docs/images/markdown-syntax.png" alt="Markdown syntax shown"></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Slides</b>: wide sheets that grow with the text</sub></td>
    <td align="center"><sub><b>A4</b>: printer paper (US Letter in the US and Canada) with the export's margins and page breaks</sub></td>
    <td align="center"><sub><b>Show Markdown Syntax</b> on</sub></td>
  </tr>
</table>

Switch with **View ▸ Page Layout** (or the Touch Bar). The setting applies to every open note.

### Tabs, windows, folders and the sidebar

On macOS, notes open as tabs of one window (<kbd>⌘</kbd><kbd>T</kbd> makes a new note in a new tab). On
Windows, each note opens in its own window. The sidebar has three sections:

- **Notes:** the notes folder as a tree. <kbd>⇧</kbd><kbd>⌘</kbd><kbd>N</kbd> creates a folder in the
  selected one; drag notes between folders; right-click to rename, show in Finder / Explorer or move to
  the Trash / Recycle Bin.
- **Pages:** the current note's pages with thumbnails.
- **Study:** imported course material, **Generate study plan**, and every flashcard deck with the number
  of cards to study (due and new).

### Export

**File ▸ Export** (or the `⋯` toolbar menu on macOS):

| Format | What you get |
|---|---|
| **PDF (Blueprint)** | Blue sheets with a grid, a frame and a title block (title, date, sheet *n / m*). |
| **PDF (Print)** | The same typesetting on white paper. |
| **Markdown…** | Plain Markdown: front matter dropped, drawings replaced by `*[drawing omitted]*`, decks as lists, pages separated by rules. |

PDFs are A4 (US Letter in the US and Canada) and break pages at each note page. Drawings are not part of
the PDF yet.

<p align="center"><img src="docs/images/pdf-export.png" width="720" alt="Page one of a note exported as PDF, in Blueprint style with a title block and in Print style on white paper"></p>

### Keyboard shortcuts

<details open>
<summary><b>Shortcuts from the menus</b></summary>

| Action | macOS | Windows | Menu |
|---|---|---|---|
| New note | <kbd>⌘</kbd><kbd>N</kbd> | <kbd>Ctrl</kbd><kbd>N</kbd> | File |
| New tab (new note) | <kbd>⌘</kbd><kbd>T</kbd> | | File |
| New folder | <kbd>⇧</kbd><kbd>⌘</kbd><kbd>N</kbd> | <kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>N</kbd> | File |
| Open… | <kbd>⌘</kbd><kbd>O</kbd> | <kbd>Ctrl</kbd><kbd>O</kbd> | File |
| Save | | <kbd>Ctrl</kbd><kbd>S</kbd> | File |
| Duplicate | <kbd>⇧</kbd><kbd>⌘</kbd><kbd>S</kbd> | <kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>S</kbd> | File |
| Show in Finder / Explorer | <kbd>⌥</kbd><kbd>⌘</kbd><kbd>R</kbd> | <kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>R</kbd> | File |
| Settings | <kbd>⌘</kbd><kbd>,</kbd> | <kbd>Ctrl</kbd><kbd>,</kbd> | Whiteprint / File |
| Undo / Redo | <kbd>⌘</kbd><kbd>Z</kbd> / <kbd>⇧</kbd><kbd>⌘</kbd><kbd>Z</kbd> | <kbd>Ctrl</kbd><kbd>Z</kbd> / <kbd>Ctrl</kbd><kbd>Y</kbd> | Edit |
| Find / Find and replace | <kbd>⌘</kbd><kbd>F</kbd> / <kbd>⌥</kbd><kbd>⌘</kbd><kbd>F</kbd> | | Edit ▸ Find |
| Command palette | <kbd>⌘</kbd><kbd>K</kbd> | <kbd>Ctrl</kbd><kbd>K</kbd> | View |
| Toggle sidebar | <kbd>⌘</kbd><kbd>\\</kbd> | <kbd>Ctrl</kbd><kbd>\\</kbd> | View |
| Show Markdown syntax | <kbd>⇧</kbd><kbd>⌘</kbd><kbd>M</kbd> | <kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>M</kbd> | View |
| Full screen | <kbd>⌃</kbd><kbd>⌘</kbd><kbd>F</kbd> | <kbd>F11</kbd> | View |
| Heading 1 / 2 / 3 | <kbd>⌥</kbd><kbd>⌘</kbd><kbd>1</kbd> / <kbd>2</kbd> / <kbd>3</kbd> | <kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>1</kbd> / <kbd>2</kbd> / <kbd>3</kbd> | Format |
| Body text | <kbd>⌥</kbd><kbd>⌘</kbd><kbd>0</kbd> | <kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>0</kbd> | Format |
| Bold / Italic | <kbd>⌘</kbd><kbd>B</kbd> / <kbd>⌘</kbd><kbd>I</kbd> | <kbd>Ctrl</kbd><kbd>B</kbd> / <kbd>Ctrl</kbd><kbd>I</kbd> | Format |
| Bulleted / Numbered list | <kbd>⌥</kbd><kbd>⌘</kbd><kbd>8</kbd> / <kbd>7</kbd> | <kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>8</kbd> / <kbd>7</kbd> | Format |
| Checklist | <kbd>⌥</kbd><kbd>⌘</kbd><kbd>9</kbd> | <kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>9</kbd> | Format |
| Add page | <kbd>⌥</kbd><kbd>⌘</kbd><kbd>N</kbd> | <kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>N</kbd> | Note |
| Insert drawing | <kbd>⇧</kbd><kbd>⌘</kbd><kbd>D</kbd> | <kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>D</kbd> | Note |
| Insert flashcards | <kbd>⇧</kbd><kbd>⌘</kbd><kbd>F</kbd> | <kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>F</kbd> | Note |
| Minimize | <kbd>⌘</kbd><kbd>M</kbd> | <kbd>Ctrl</kbd><kbd>M</kbd> | Window |
| Quit | <kbd>⌘</kbd><kbd>Q</kbd> | <kbd>Ctrl</kbd><kbd>Q</kbd> | Whiteprint / File |

Inline code, quote, code block and divider are in the **Format** menu without a shortcut.
**Note ▸ Study Flashcards…** and **Note ▸ Generate Study Plan…** start studying and a study plan run.

</details>

<details>
<summary><b>In the editor, the study window and popovers</b></summary>

| Where | Keys (macOS; Windows uses <kbd>Ctrl</kbd>, <kbd>Alt</kbd>, <kbd>Enter</kbd>) |
|---|---|
| Lists | <kbd>↩</kbd> continue / end, <kbd>⇥</kbd> indent, <kbd>⇧</kbd><kbd>⇥</kbd> outdent |
| `/` menu | <kbd>↑</kbd> <kbd>↓</kbd> choose, <kbd>↩</kbd> insert, <kbd>esc</kbd> close |
| Drawing popover | <kbd>⌘</kbd><kbd>↩</kbd> apply; clicking outside or <kbd>esc</kbd> also applies |
| Deck editor | <kbd>⌘</kbd><kbd>↩</kbd> done, <kbd>⌥</kbd><kbd>↩</kbd> line break in a question or answer |
| Command palette | <kbd>↑</kbd> <kbd>↓</kbd> choose, <kbd>↩</kbd> run, <kbd>esc</kbd> close |
| Study Flashcards | <kbd>Space</kbd> or <kbd>↩</kbd> flip, <kbd>1</kbd>–<kbd>4</kbd> Again / Hard / Good / Easy, <kbd>esc</kbd> close |

</details>

### Touch Bar (macOS)

On a MacBook Pro with a Touch Bar, the note window shows **Toggle Sidebar**, **Command Palette** and
**New Note** on the left, a **Slides | A4** switch, a **Markdown** toggle and **Study** on the right. While
you type, the middle holds a text style popover (H1, H2, H3, Body), **Bold**, **Italic**, **Code**,
**Checklist**, **Bulleted list**, **Insert drawing**, **Insert flashcards** and **New page**. Rearrange it
with **View ▸ Customize Touch Bar…**.

## Connect Claude

Whiteprint ships a small helper, `whiteprint-mcp`, with the app. Claude Code or Claude Desktop starts it
as an MCP server; it forwards each tool call to the running app (starting the app if needed), so Claude's
edits show up live in your open notes.

<img src="docs/images/settings-ai.png" width="360" align="right" alt="Settings, AI tab: study plan provider, Grok key, Claude Code status with an Add button and command, Claude Desktop with a Connect button and JSON snippet">

**The easy way:** open **Settings… ▸ AI** (in the **Whiteprint** menu on macOS, **File** on Windows) and
click

- **Add Whiteprint to Claude Code**, which runs `claude mcp add` for your user after you confirm, or
- **Connect Claude Desktop**, which adds Whiteprint to Claude Desktop's config (other servers are kept
  and the old file is saved as `claude_desktop_config.json.backup`). Restart Claude Desktop afterwards.

<br clear="right">

**By hand, Claude Code:**

```sh
# macOS
claude mcp add --scope user whiteprint -- /Applications/Whiteprint.app/Contents/MacOS/whiteprint-mcp
```

```powershell
# Windows
claude mcp add --scope user whiteprint -- "$env:LOCALAPPDATA\Programs\Whiteprint\whiteprint-mcp.exe"
```

**By hand, Claude Desktop:** add this to `claude_desktop_config.json`
(`~/Library/Application Support/Claude/` on macOS, `%APPDATA%\Claude\` on Windows):

```jsonc
{
  "mcpServers": {
    "whiteprint": {
      // macOS
      "command": "/Applications/Whiteprint.app/Contents/MacOS/whiteprint-mcp"
      // Windows: "C:\\Users\\<you>\\AppData\\Local\\Programs\\Whiteprint\\whiteprint-mcp.exe"
    }
  }
}
```

A Windows all-users install lives in `C:\Program Files\Whiteprint` instead.

**Try asking:**

> *"List my Whiteprint notes and add a diagram of our checkout flow to System design, page 1."*
>
> *"Make a note in Courses/Networks summarising TCP congestion control, with a small diagram."*
>
> *"Turn my note Lecture 3 – TCP into 10 flashcards."*
>
> *"Import ~/Downloads/Lecture4.pdf and build me a study plan."*

<details>
<summary><b>MCP tools</b> (15)</summary>

Note ids (`n1`, `n2`, …) come from `list_notes` and last for the app session; pages are 1-based; drawing
ids (`d1`, …) are per note. Replies are short (`ok d3`); notes are never echoed back.

| Tool | Arguments | Does |
|---|---|---|
| `list_notes` | | Lists notes: id, title, pages |
| `read_note` | `note`, `page?` | Reads a note's `.wprint` source, or one page |
| `create_note` | `title`, `markdown?`, `folder?` | Creates a note, optionally in a folder such as `Courses/Networks` |
| `write` | `note`, `page`, `markdown`, `mode` (`append` \| `replace`) | Appends to or replaces a page's Markdown |
| `draw` | `note`, `page`, `dsl`, `after?` | Adds a drawing; returns its id and one line per compile error |
| `edit_drawing` | `note`, `drawing`, `dsl` | Replaces a drawing's source |
| `delete_drawing` | `note`, `drawing` | Deletes a drawing |
| `add_page` | `note` | Adds a page at the end |
| `create_flashcards` | `note?`, `page?`, `title`, `cards[]` | Adds a deck to a note's page (default: last), or to a new note |
| `import_document` | `path` | Imports a PDF, DOCX, DOC or PPTX for study |
| `list_imports` | | Lists imports: id, name, pages, chunks, status |
| `read_chunk` | `import`, `chunk` | Reads one chunk, each line tagged with its source (`slide 14`, `p. 6`) |
| `save_points` | `import`, `chunk`, `points[]` | Saves the key points of a chunk (`text`, `importance`, `ref`, `topic?`) |
| `get_points` | `import?` | Lists every saved point, compactly |
| `build_study_plan` | `imports[]`, `plan` | Writes the final study plan as a new note |

A drawing in which nothing compiles is refused, so a typo can't blank a diagram.

</details>

**Also served:** the resource **`whiteprint://dsl`** (the drawing language reference, read once before
the first drawing instead of being repeated in every tool description) and two prompts:
**`study_plan`** (optional `imports`) builds a study plan from imported material, and **`flashcards`**
(`source`, a note or import id such as `n3` or `i2`) makes a deck. In Claude Desktop they appear in
the prompt menu.

### Why drawings are cheap

Claude doesn't send SVG or coordinates for every line. It writes a few short statements; Whiteprint lays
them out, sizes boxes to their labels and clips arrows to shape edges. This diagram is six statements:

<p align="center"><img src="docs/images/drawing-language.png" alt="Drawing language source on the left: flow Web>API>Orders>Postgres, db Postgres, two boxes and two arrows; on the right the rendered diagram on a blueprint grid"></p>

Shapes: `box`, `circle`, `db`, `text`. Lines: `arrow a>b>c`, `line a-b`, free polylines, `path`, `dim`.
Layout: `flow`, `row`, `col`, `group`. Styles: `dashed`, `thick`, `bold`. Units are grid squares. Errors
come back as one line each (`line 3: unknown id 'x'`). The full grammar is in
[docs/DSL.md](docs/DSL.md) and in the app under **Help ▸ Drawing Language**.

## Study plans and flashcards

```mermaid
flowchart LR
    A["PDF · DOCX · DOC · PPTX"] -->|"extracted on your computer<br>(OCR for scanned pages)"| B["Imports<br>chunks with source refs"]
    B -->|"read_chunk → save_points"| C["Claude Code<br>or Grok"]
    C -->|"get_points → build_study_plan"| D["Study plan note<br>★ ○ ✕ · to-do · flashcards"]
    D --> E["Study Flashcards<br>spaced repetition"]
```

1. **Import.** **File ▸ Import for Study Plan…** opens the study panel. Drop PDF, Word (`.docx`, `.doc`)
   or PowerPoint (`.pptx`) files in. Text is extracted locally, scanned PDF pages go through OCR (Vision
   on macOS, the OCR built into Windows), and repeated headers, footers and slide numbers are removed.
2. **Generate.** Click **Generate study plan**. With **Claude Code** (the default) Whiteprint runs your
   installed, logged-in `claude` in the background, limited to Whiteprint's tools, so usage counts
   against your own subscription. With **Grok** it calls xAI's API with the key you saved in **Settings
   ▸ AI** (kept in the macOS Keychain or Windows Credential Manager). Progress streams into the panel.
3. **Read the plan.** The finished note opens in a new tab (macOS) or window (Windows): an overview, a
   learning path with study time per module, every point tiered **★ must know**, **○ good to know** or
   **✕ can skip** with its source (`Lecture3.pptx · slide 14`), a `- [ ]` to-do list of assignments and
   deadlines, a flashcard deck and optional diagrams.
4. **Study.** **Study** on a deck (or **Note ▸ Study Flashcards…**) shows one card at a time. Rate each
   answer **Again / Hard / Good / Easy**; a small SM-2 scheduler brings cards back after 10 minutes or
   after days that grow with each success. The sidebar shows how many cards are waiting in each deck.

Points are saved per chunk inside Whiteprint, so long course packs don't overflow one conversation and
an interrupted run picks up where it stopped. Claude Desktop users can run the same pipeline with the
**`study_plan`** prompt.

<table>
  <tr>
    <td width="50%"><img src="docs/images/study-panel.png" alt="The study panel sheet with a drop zone, two imported files with read progress, a privacy note and the Generate study plan button"></td>
    <td width="50%"><img src="docs/images/study-plan.png" alt="A generated study plan note: overview, legend and a learning path with starred must-know points and source references"></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/flashcards-question.png" alt="Study Flashcards window showing a question on a blueprint card"></td>
    <td width="50%"><img src="docs/images/flashcards-answer.png" alt="The card flipped: the answer with its source, and Again, Hard, Good, Easy buttons with their intervals"></td>
  </tr>
</table>

> [!IMPORTANT]
> **Privacy.** Your files never leave your computer. Only the extracted text is shared, and only with the
> AI client you chose: your own Claude Code or Claude Desktop, or xAI with your own key. Whiteprint never
> sees your Claude login and makes no network requests of its own except to xAI when you pick Grok.
> **Settings ▸ Study ▸ Clear Extracted Text Cache…** deletes every import.

<details>
<summary><b>Flashcard decks in notes</b></summary>

<img src="docs/images/flashcard-deck.png" alt="A flashcard deck block on a page with four cards, one showing its answer and source, and Study and Edit buttons">

</details>

## File format

A note is one plain-text `.wprint` file (UTType `io.github.l1203012.whiteprint.note` on macOS): front
matter, Markdown, `+++page` between pages, and fenced `wp` (drawing) and `cards` (flashcard deck)
blocks. It diffs well, syncs with anything that syncs files, and Claude can read and edit it cheaply.
The format is identical on macOS and Windows.

````text
---
whiteprint: 1
title: Lecture 3 – TCP
---
# Lecture 3 – TCP

Reliable, ordered byte streams on top of IP.

```wp id=d1
flow SYN>SYN_ACK>ACK
text "Client and server agree on sequence numbers first"
```

- [x] Re-watch the slow start animation
- [ ] Exercise 3.4

+++page

```cards id=c1
# TCP basics
Q: Name the three segments of the handshake.
A: SYN, SYN-ACK, ACK.
ref: Lecture 3 – TCP.pdf · p. 7
```
````

## Architecture

```mermaid
flowchart LR
    subgraph Clients["Your AI client"]
        CC["Claude Code"]
        CD["Claude Desktop"]
    end
    subgraph App["Whiteprint"]
        MCP["whiteprint-mcp<br>(stdio MCP server)"]
        WP["Whiteprint<br>(AppKit app on macOS,<br>Qt Widgets app on Windows)"]
    end
    CC -- "MCP over stdio" --> MCP
    CD -- "MCP over stdio" --> MCP
    MCP -- "one JSON line per request<br>Unix socket (macOS) /<br>named pipe (Windows)" --> WP
    WP -- "read / write" --> Notes[("Documents/Whiteprint<br>*.wprint")]
    WP -- "claude -p … (one-click study plans)" --> CC
    WP -. "HTTPS, your key (optional)" .-> Grok["xAI Grok API"]
```

The app owns all state; the helper is a thin, stateless bridge that launches the app if it isn't
running. The MCP server is a small hand-written JSON-RPC 2.0 handler. The socket is
`~/Library/Application Support/Whiteprint/mcp.sock` on macOS and `\\.\pipe\whiteprint-<user>` on
Windows. The Mac app has no dependencies beyond the system frameworks; the Windows app depends only on Qt,
linked dynamically (LGPL).

```mermaid
flowchart BT
    Core["Core"]
    Extract["Extract"]
    Render["Render"] --> Core
    Bridge["Bridge"] --> Core
    Study["Study"] --> Core
    Study --> Extract
    Editor["Editor"] --> Core
    Editor --> Render
    App["App"] --> Editor & Study & Bridge & Render & Extract & Core
    Helper["whiteprint-mcp"] --> Bridge & Core
```

The Windows port mirrors the Swift targets one to one: each module is a Swift target under `Sources/`
on macOS and a static library `wp_<dir>` (namespace `wp`) under `windows/` on Windows.

| macOS (`Sources/`) | Windows (`windows/`) | What it does |
|---|---|---|
| `WhiteprintCore` | `core` | `.wprint` format, note editing, Markdown export, drawing language compiler, study plan model. No GUI code. |
| `WhiteprintExtract` | `extract` | Local text extraction from PDF (with OCR), DOCX/DOC and PPTX; cleaning and chunking with source refs |
| `WhiteprintRender` | `render` | Drawing renderer, blueprint background, Markdown styling, PDF export, page thumbnails |
| `WhiteprintEditor` | `editor` | The Notion-style page editor: blocks, slash menu, drawing and deck popovers (and the Touch Bar on macOS) |
| `WhiteprintBridge` | `bridge` | MCP server, tool catalog and the socket / pipe between `whiteprint-mcp` and the app |
| `WhiteprintStudy` | `study` | Imports and saved points, study plan notes, the Claude Code and Grok runners |
| `WhiteprintApp` | `app` | The app: documents, windows and tabs, sidebar, command palette, study panel, flashcards, settings |
| `whiteprint-mcp` | `mcp` | The stdio helper that Claude Code and Claude Desktop start |

How the port is laid out and what differs is in [docs/WINDOWS_PORT.md](docs/WINDOWS_PORT.md). The
differences are platform substitutes: a named pipe for the Unix socket, Windows OCR for Vision,
Credential Manager for the Keychain, the registry for preferences, <kbd>Ctrl</kbd> for <kbd>⌘</kbd>.

## Development

### macOS

**Requirements:** macOS 13+ and the Xcode **Command Line Tools** (`xcode-select --install`) with
Swift 5.8 or later. Xcode is optional: without it, the scripts build with plain `swiftc` and run the
tests through a small XCTest stand-in.

```sh
Scripts/build.sh app            # .build/Whiteprint.app for this Mac's architecture
Scripts/build.sh                # every module and both executables
Scripts/test.sh                 # every module's tests
Scripts/test.sh WhiteprintApp   # one module
Scripts/check-targets.sh        # Package.swift and Scripts/targets.sh agree
Scripts/release.sh 0.1.0        # universal .build/release/Whiteprint-0.1.0.dmg + .sha256
ARCHS=arm64 Scripts/release.sh 0.1.0    # Whiteprint-0.1.0-AppleSilicon.dmg (x86_64: -Intel.dmg)
```

`Scripts/test.sh` uses `swift test` when Xcode's XCTest is available and the stand-in otherwise; force
one with `WHITEPRINT_TEST_RUNNER=swiftpm` or `WHITEPRINT_TEST_RUNNER=shim`. With Xcode installed,
`swift build` and `swift test` work too. `CONFIG=release` and `ARCHS="arm64 x86_64"` make an optimised
universal build.

### Windows

**Requirements:** Windows 10 or 11, Qt 6.8 for MinGW (installed to `C:\Qt`, see `windows/env.ps1`),
CMake and Ninja. Inno Setup 6 (`winget install JRSoftware.InnoSetup`) only for the installer.

```powershell
windows\build.ps1                              # configure, build, run every module's tests (.build\windows)
windows\build.ps1 -NoTest                      # build only
windows\installer\package.ps1 -Version 0.1.1   # .build\windows-release\Whiteprint-0.1.1-Setup.exe + .sha256
```

Run them with `powershell -ExecutionPolicy Bypass -File <script>` if scripts are blocked. Tests are Qt
Test executables, one per `<module>/tests/*.cpp`, driven by `ctest`; run one with
`ctest --test-dir .build\windows -R editor`. In CI they run with `QT_QPA_PLATFORM=offscreen`.

### Running in isolation

These environment variables work on both platforms and are handy for trying things without touching your
real notes:

| Variable | Effect |
|---|---|
| `WHITEPRINT_NOTES_DIR` | Use another notes folder |
| `WHITEPRINT_SOCKET` | Use another socket (macOS: a path) or named pipe (Windows: a name or a full `\\.\pipe\...` path), for the app and `whiteprint-mcp` |
| `WHITEPRINT_DEFAULTS_SUITE` | Keep preferences in a separate defaults domain (macOS) or registry key (Windows) |
| `WHITEPRINT_SCREENSHOTS=<dir>` | macOS only: run the scripted screenshot tour, save PNGs into `<dir>` and quit |

```sh
# macOS
WHITEPRINT_NOTES_DIR=/tmp/notes WHITEPRINT_SOCKET=/tmp/wp.sock WHITEPRINT_DEFAULTS_SUITE=wp-dev \
  .build/Whiteprint.app/Contents/MacOS/Whiteprint
```

```powershell
# Windows
$env:WHITEPRINT_NOTES_DIR = "C:\temp\notes"; $env:WHITEPRINT_SOCKET = "wp-test"; $env:WHITEPRINT_DEFAULTS_SUITE = "wp-dev"
.build\windows\Whiteprint.exe
```

## CI/CD

```mermaid
flowchart LR
    PR["Pull request"] --> CI & WCI
    Push["Push to main"] --> CI & Edge & WCI & WEdge
    Tag["Tag vX.Y.Z[-pre]"] --> Release & WRelease
    WTag["Tag windows-vX.Y.Z[-pre]"] --> WRelease

    subgraph Mac["macOS"]
        CI["CI · ci.yml<br>test (Apple Silicon, Intel) · clt-path · lint · app"]
        Edge["Edge build · edge.yml<br>DMG"]
        Release["Release · release.yml<br>test → sign* → DMGs (Apple Silicon, Intel) → notarize*"]
    end

    subgraph Win["Windows"]
        WCI["Windows CI · windows-ci.yml<br>build + ctest"]
        WEdge["Windows Edge build · windows-edge.yml<br>installer"]
        WRelease["Windows Release · windows-release.yml<br>build → test → Inno Setup"]
    end

    Release --> Draft["Draft GitHub release<br>installer + .sha256 + install notes"]
    WRelease --> Draft
```

<sub>\* when the signing and notarization secrets are set.</sub>

| Workflow | Runs on | What it does |
|---|---|---|
| **CI** (`ci.yml`) | Pushes and pull requests to `main` | `test (Apple Silicon)` on `macos-14` and `test (Intel)` on `macos-15-intel` with SwiftPM; `clt-path` runs `Scripts/test.sh` the Command-Line-Tools way; `lint` runs ShellCheck, actionlint and `Scripts/check-targets.sh`; `app` builds a universal debug app and uploads `Whiteprint-app-<short sha>.zip` (14 days). |
| **Edge build** (`edge.yml`) | Every push to `main` | Builds the universal DMG and uploads `Whiteprint-<version>-edge.<run number>` (DMG + `.sha256`, 14 days). No tags or releases. |
| **Release** (`release.yml`) | `vX.Y.Z` and `vX.Y.Z-pre` tags | Tests, signs with a Developer ID and notarizes when the secrets exist, builds an Apple Silicon and an Intel DMG with checksums, and creates a **draft** release whose notes include the download table and install steps from `.github/release-notes.md`. Run by hand with `tag` to add DMGs to an existing release. |
| **Windows CI** (`windows-ci.yml`) | Pushes and pull requests touching `windows/` | Installs Qt 6.8 and MinGW, builds with CMake and Ninja, and runs every test with `ctest`. |
| **Windows Edge build** (`windows-edge.yml`) | Pushes touching `windows/` | Builds the installer and uploads `Whiteprint-<version>-edge.<run number>` (Setup.exe + `.sha256`, 14 days). |
| **Windows Release** (`windows-release.yml`) | `vX.Y.Z` and `windows-vX.Y.Z` tags (and `-pre`) | Builds, tests and packages the installer. For a `vX.Y.Z` tag it goes into the same release as the DMGs; a `windows-vX.Y.Z` tag makes a Windows-only release with `.github/windows-release-notes.md`. |
| **Dependabot** | Weekly | Keeps the GitHub Actions used by the workflows up to date. |

Bug and feature issue forms and a pull request template live in `.github/`. Releasing is described in
[docs/RELEASING.md](docs/RELEASING.md) (macOS) and [docs/RELEASING_WINDOWS.md](docs/RELEASING_WINDOWS.md)
(Windows, including the winget submission).

## Roadmap

Planned or under consideration:

- [ ] Signed builds on both platforms (Developer ID + notarization, Authenticode), so the first-launch
      step goes away
- [ ] `winget install Whiteprint.Whiteprint` (submitted, waiting for the winget-pkgs merge)
- [ ] Drawings in PDF export
- [ ] Freehand drawing, and technical sketches with arcs and angles
- [ ] Images and tables in notes
- [x] Automatic updates on macOS (checked at launch, from GitHub Releases)
- [ ] iCloud sync
- [ ] Day-by-day study schedules from deadlines found in course material
- [ ] Windows ARM64 build

See [docs/BETA_PLAN.md](docs/BETA_PLAN.md) for the original plan and open questions.

## Contributing

Bug reports and ideas are welcome: open an
[issue](https://github.com/l1203012/Whiteprint-Notetaking-Application/issues/new/choose) with the bug
or feature form. For code changes:

1. Fork, branch from `main`, and keep the change focused.
2. Before pushing, run `Scripts/test.sh` (and `Scripts/check-targets.sh` if you added or moved a module)
   for macOS changes, and `windows\build.ps1` for Windows changes.
3. Open a pull request against `main`; CI tests the Mac app on Intel and Apple Silicon, and the
   Windows app when `windows/` changes.

A feature should land on both platforms where it can. Keep `WhiteprintCore` / `core` free of GUI code,
keep notes byte-compatible between the two apps, keep MCP replies short, and keep the pages blue.

## License

[MIT](LICENSE) © 2026 Adam
