<!--
  Install section appended to every Windows release's generated notes by
  .github/workflows/windows-release.yml. Placeholders: {{VERSION}}, {{SETUP}}, {{SHA256}},
  {{REPOSITORY}}. This comment is dropped.
-->
## Install (Windows)

**Requirements:** Windows 10 or 11, 64-bit.

1. Download **{{SETUP}}** from the assets below (and **{{SETUP}}.sha256** if you want to verify it).
2. Check the download in PowerShell, from the folder you saved it to:

   ```powershell
   (Get-FileHash {{SETUP}} -Algorithm SHA256).Hash -eq (Get-Content {{SETUP}}.sha256).Split(' ')[0]
   ```

   It should print `True`. The SHA-256 is `{{SHA256}}`.
3. Run the installer. It installs for your user only; no administrator rights are needed.

Or with winget, once this version is merged into the winget community repository:

```powershell
winget install Whiteprint.Whiteprint
```

### First launch

The installer is not code-signed yet, so Windows SmartScreen may say *"Windows protected your PC"*.
Click **More info**, then **Run anyway**. After that it installs and opens normally.

Found a problem? [Open an issue](https://github.com/{{REPOSITORY}}/issues/new/choose) and include
your Windows version and the Whiteprint version ({{VERSION}}).
