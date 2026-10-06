<!--
  Install section appended to every release's generated notes by
  .github/workflows/release.yml. Placeholders: {{VERSION}}, {{TAG}}, {{DMG_ARM}},
  {{DMG_INTEL}}, {{SHA256_ARM}}, {{SHA256_INTEL}}, {{SETUP}}, {{REPOSITORY}}. The block between the "unsigned" markers is
  dropped when the build is notarized; this comment is dropped always.
-->
## Downloads

| Platform | File |
|---|---|
| macOS, Apple Silicon (M1, M2, M3, …) | **{{DMG_ARM}}** |
| macOS, Intel | **{{DMG_INTEL}}** |
| Windows 10 / 11, 64-bit | **{{SETUP}}** |

Not sure which Mac you have? Apple menu → About This Mac shows *Chip: Apple M…* (Apple Silicon) or
*Processor: … Intel …* (Intel).

## Install on macOS

**Requirements:** macOS 13 Ventura or later.

1. Download the DMG for your Mac from the assets below (and its `.sha256` if you want to verify it).
2. Check the download in Terminal, from the folder you saved both files to:

   ```sh
   shasum -a 256 -c {{DMG_ARM}}.sha256     # or {{DMG_INTEL}}.sha256
   ```

   It should print `…: OK`. The SHA-256 is `{{SHA256_ARM}}` (Apple Silicon) or `{{SHA256_INTEL}}` (Intel).
3. Open the DMG and drag **Whiteprint** onto **Applications**.

<!-- unsigned -->
### First launch

This build is not notarized by Apple, so macOS blocks it the first time with *"Whiteprint" cannot
be opened because Apple cannot check it for malicious software* (or *"Whiteprint" Not Opened*).
Open it once in either of these ways; after that it opens normally.

- **Right-click → Open:** in Finder, Control-click (or right-click) Whiteprint in Applications,
  choose **Open**, then click **Open** in the dialog.
- **Open Anyway:** open Whiteprint normally and dismiss the warning, then go to
  **System Settings → Privacy & Security**, scroll to *"Whiteprint" was blocked…*, click
  **Open Anyway** and confirm with **Open**.

On macOS 15 Sequoia and later only the second way works.
<!-- /unsigned -->

## Install on Windows

**Requirements:** Windows 10 or 11, 64-bit.

1. Download **{{SETUP}}** from the assets below (and its `.sha256` if you want to verify it).
2. Run it. It installs for your user only; no administrator rights are needed.
3. The installer is not code-signed yet, so SmartScreen may say *"Windows protected your PC"*. Click
   **More info**, then **Run anyway**.

Found a problem? [Open an issue](https://github.com/{{REPOSITORY}}/issues/new/choose) and include
your OS version, whether your Mac is Intel or Apple Silicon, and the Whiteprint version ({{VERSION}}).
