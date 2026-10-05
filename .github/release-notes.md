<!--
  Install section appended to every release's generated notes by
  .github/workflows/release.yml. Placeholders: {{VERSION}}, {{TAG}}, {{DMG}},
  {{SHA256}}, {{REPOSITORY}}. The block between the "unsigned" markers is
  dropped when the build is notarized; this comment is dropped always.
-->
## Install

**Requirements:** macOS 13 Ventura or later, on an Intel or Apple Silicon Mac (one universal app).

1. Download **{{DMG}}** from the assets below (and **{{DMG}}.sha256** if you want to verify it).
2. Check the download in Terminal, from the folder you saved both files to:

   ```sh
   shasum -a 256 -c {{DMG}}.sha256
   ```

   It should print `{{DMG}}: OK`. The SHA-256 is `{{SHA256}}`.
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

Found a problem? [Open an issue](https://github.com/{{REPOSITORY}}/issues/new/choose) and include
your macOS version, whether your Mac is Intel or Apple Silicon, and the Whiteprint version ({{VERSION}}).
