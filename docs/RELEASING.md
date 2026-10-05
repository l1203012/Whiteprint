# Releasing Whiteprint

Releases are DMGs on GitHub Releases. A pushed `vX.Y.Z` tag builds one in GitHub Actions
(`.github/workflows/release.yml`) and attaches it to a draft release for you to review and publish.

## Cutting a release

1. Bump `CFBundleShortVersionString` in `Resources/Info.plist`, e.g. `0.1.0-beta.2`, and commit.
   The build number (`CFBundleVersion`) is the commit count and needs no bump.
2. Tag the commit and push the tag:

   ```sh
   git tag v0.1.0-beta.2
   git push origin v0.1.0-beta.2
   ```

3. The **Release** workflow then:
   - runs `Scripts/test.sh`,
   - imports the Developer ID certificate and notarization key, if their secrets are set,
   - runs `Scripts/release.sh 0.1.0-beta.2`, which builds the universal app, signs it, builds
     `Whiteprint-0.1.0-beta.2.dmg`, notarizes and staples it, and writes its `.sha256`,
   - creates a **draft** release `v0.1.0-beta.2` with generated notes and uploads the DMG and
     checksum (versions with a `-`, like `-beta.2`, are marked pre-release). Re-running the
     workflow replaces the files of an existing draft.
4. Open the draft under **Releases**, edit the notes, and click **Publish release**.

The workflow can also be started from the Actions tab (**Release → Run workflow**) with an optional
version; the draft's tag is created when you publish it. Tick *Skip the Finder window layout* (or
set the repository variable `DMG_PLAIN=1`) if Finder scripting misbehaves on the runner; the DMG
then opens as a plain window with the same contents.

## Building a DMG locally

```sh
Scripts/release.sh                 # version from Info.plist
Scripts/release.sh 0.1.0-beta.2    # .build/release/Whiteprint-0.1.0-beta.2.dmg + .sha256
```

| Variable | Effect |
|---|---|
| `SIGN_IDENTITY` | `Developer ID Application: Name (TEAMID)`: hardened runtime and secure timestamp. Default: ad-hoc. |
| `NOTARY_PROFILE` | Notarize with a `notarytool store-credentials` keychain profile, then staple. |
| `NOTARY_KEY_PATH`, `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID` | Notarize with an App Store Connect API key instead. |
| `DMG_PLAIN=1` | Skip the Finder layout (window size, icon positions, background). |
| `VOLUME_ICON` | Volume icon, default `Resources/App/AppIcon.icns` when present. |

The window layout is set by scripting Finder, which briefly mounts the image and opens its window.
The first time, macOS asks whether your terminal may control Finder; without that permission the
script falls back to a plain layout. An already mounted volume named "Whiteprint" also forces the
plain layout, since Finder finds the volume by name.

Notarization needs `notarytool` and `stapler` (Xcode, or recent Command Line Tools); without them
the script says so and skips it.

The window background lives in `Resources/DMG/` and is drawn by `Scripts/make-dmg-background.swift`
(see the comment at its top for how to run it).

## Signing and notarization secrets

Without these the workflow still produces an ad-hoc signed DMG (see below). Add them under
**Settings → Secrets and variables → Actions**:

| Secret | Value |
|---|---|
| `MACOS_CERTIFICATE` | Your *Developer ID Application* certificate and private key exported as `.p12`, base64-encoded: `base64 -i DeveloperID.p12 \| pbcopy` |
| `MACOS_CERTIFICATE_PASSWORD` | The password you gave the `.p12` export |
| `NOTARY_KEY` | The App Store Connect API key (`AuthKey_XXXXXXXXXX.p8`), as is or base64-encoded |
| `NOTARY_KEY_ID` | The key's ID (the `XXXXXXXXXX`) |
| `NOTARY_ISSUER_ID` | The issuer ID shown above the keys list |

- **Certificate:** in [Certificates, Identifiers & Profiles](https://developer.apple.com/account/resources/certificates/list)
  create a *Developer ID Application* certificate (needs the Account Holder role), install it, then in
  Keychain Access select the certificate *with* its private key and choose **Export 2 items… → .p12**.
- **API key:** in [App Store Connect → Users and Access → Integrations → App Store Connect API](https://appstoreconnect.apple.com/access/integrations/api)
  create a team key with the *Developer* role and download the `.p8` (it can be downloaded once).

The workflow imports the certificate into a temporary keychain, signs with the first Developer ID
Application identity in it, and deletes the keychain and key file at the end. Notarization without
a signing certificate fails, since Apple only notarizes Developer ID signed apps.

For notarizing locally, store the key once as a keychain profile:

```sh
xcrun notarytool store-credentials whiteprint --key AuthKey_XXXXXXXXXX.p8 --key-id XXXXXXXXXX --issuer <issuer-id>
SIGN_IDENTITY="Developer ID Application: Name (TEAMID)" NOTARY_PROFILE=whiteprint Scripts/release.sh
```

Check a signed, notarized build with `spctl -a -vv .build/release/Whiteprint.app` (expect
`accepted, source=Notarized Developer ID`) and `xcrun stapler validate .build/release/Whiteprint-*.dmg`.

## Opening an unsigned beta

Beta builds without a Developer ID signature are ad-hoc signed, so Gatekeeper blocks them the first
time with *"Whiteprint" cannot be opened because Apple cannot check it for malicious software*
(or *"Whiteprint" Not Opened*). After dragging Whiteprint to Applications, open it once in one of
these ways; later launches work normally.

- **Right-click → Open.** In Finder, Control-click (or right-click) Whiteprint in Applications,
  choose **Open**, then **Open** again in the dialog.
- **Privacy & Security.** Open Whiteprint normally and dismiss the warning, then go to
  **System Settings → Privacy & Security**, scroll to *"Whiteprint" was blocked…* and click
  **Open Anyway**, then **Open**.
- **Last resort, Terminal.** Remove the quarantine flag the browser added, then open the app:

  ```sh
  xattr -dr com.apple.quarantine /Applications/Whiteprint.app
  ```

  Only do this for a DMG you downloaded from this repository's Releases page and whose checksum
  matches: `shasum -a 256 Whiteprint-<version>.dmg` against the `.sha256` file.
