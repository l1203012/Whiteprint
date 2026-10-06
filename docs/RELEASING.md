# Building, testing and releasing Whiteprint

Everything runs in GitHub Actions on GitHub-hosted Macs. Three workflows cover it:

| Workflow | File | Runs on | Produces |
|---|---|---|---|
| **CI** | `.github/workflows/ci.yml` | pushes and pull requests to `main`, or by hand | test results, lint, a zipped app build per commit |
| **Edge build** | `.github/workflows/edge.yml` | pushes to `main`, or by hand | an install DMG per commit, kept 14 days |
| **Release** | `.github/workflows/release.yml` | pushed `v*` tags, or by hand | a draft GitHub Release with the DMG and its checksum |

Dependabot (`.github/dependabot.yml`) opens one grouped pull request a week when the actions the
workflows use have new versions.

## CI

| Job (check name) | Runner | What it does |
|---|---|---|
| `test (Apple Silicon)` | `macos-14` | `Scripts/test.sh` through SwiftPM (`swift test`) |
| `test (Intel)` | `macos-15-intel` | the same on an Intel Mac, which is what the maintainer develops on |
| `clt-path` | `macos-14` | `Scripts/build.sh`, then `Scripts/test.sh` with `WHITEPRINT_TEST_RUNNER=shim` |
| `lint` | `ubuntu-latest` | ShellCheck on `Scripts/*.sh`, actionlint on the workflows, `Scripts/check-targets.sh` |
| `app` | `macos-14` | after both `test` jobs: `Scripts/build.sh app` for arm64 and x86_64, zipped and uploaded |

- **SwiftPM cache.** The `test` jobs cache `.build`, keyed by runner OS and architecture, the
  Swift toolchain, `Package.swift` and the hashes of `Sources/` and `Tests/`. A change to the
  sources falls back to the newest cache for the same runner and toolchain, so only what changed
  is rebuilt.
- **The Command Line Tools path.** On a Mac with only the Command Line Tools, `swift test` can't
  find XCTest, so `Scripts/build.sh` compiles the modules with plain `swiftc` and `Scripts/test.sh`
  runs each suite against a small XCTest stand-in (`Scripts/xctest-shim`). The runners have Xcode,
  so `clt-path` forces the stand-in with `WHITEPRINT_TEST_RUNNER=shim` to keep that path working.
  `Scripts/test.sh` reads the variable as `auto` (default: SwiftPM when XCTest is available,
  else the stand-in), `swiftpm` or `shim`.
- **Lint.** ShellCheck 0.11.0 and actionlint 1.7.12 are downloaded from their GitHub releases and
  checked against pinned SHA-256 sums; bump the versions and sums in `ci.yml` together. To run
  the same checks locally:

  ```sh
  shellcheck -x Scripts/*.sh
  actionlint
  Scripts/check-targets.sh   # Scripts/targets.sh describes the same module graph as Package.swift
  ```

- **App builds for testers.** Each run uploads `Whiteprint-app-<short commit>.zip` (kept 14 days)
  under *Artifacts* on the run's summary page, so a pull request can be tried before it is merged.
  It is a debug build for both architectures, ad-hoc signed; open it with right-click → **Open**
  the first time. Downloading artifacts needs a GitHub account.
- **Cancelling.** A new push to a pull request cancels the older runs for it. Runs for pushes to
  `main` are never cancelled, so every commit there gets a result.

### Branch protection (recommended)

Under **Settings → Branches** (or **Rules → Rulesets**), protect `main` and require these
status checks before merging; they appear in the list once CI has run on a pull request:

- `CI / test (Apple Silicon)`
- `CI / test (Intel)`
- `CI / clt-path`
- `CI / lint`
- `CI / app`

Also worth enabling: require a pull request before merging, and require branches to be up to
date. The Edge build and Release workflows don't run on pull requests, so don't require them.

## Edge builds

Every push to `main` also runs **Edge build**, which builds the universal install DMG with
`Scripts/release.sh` as version `<Info.plist version>-edge.<run number>`, e.g.
`0.1.0-beta.1-edge.42`. The DMG and its `.sha256` are uploaded as the artifact
`Whiteprint-<version>` (kept 14 days), and the run summary has download and install steps. It
uses the plain DMG window (`DMG_PLAIN=1`), since scripting Finder is unreliable on hosted
runners, and is ad-hoc signed. It creates no tags or releases. A newer push cancels a running
edge build.

## Cutting a release

1. Bump `CFBundleShortVersionString` in `Resources/Info.plist`, e.g. `0.1.0-beta.2`, and commit.
   The build number (`CFBundleVersion`) is the commit count and needs no bump.
2. Tag the commit and push the tag. Tags must look like `vMAJOR.MINOR.PATCH` or
   `vMAJOR.MINOR.PATCH-prerelease`, e.g. `v1.0.0` or `v0.1.0-beta.2`:

   ```sh
   git tag v0.1.0-beta.2
   git push origin v0.1.0-beta.2
   ```

3. The **Release** workflow then:
   - checks the tag's format and warns when it differs from the version in `Info.plist`,
   - runs `Scripts/test.sh`,
   - imports the Developer ID certificate and notarization key, if their secrets are set,
   - runs `Scripts/release.sh 0.1.0-beta.2`, which builds the universal app, signs it, builds
     `Whiteprint-0.1.0-beta.2.dmg`, notarizes and staples it, and writes its `.sha256`,
   - writes the release notes: GitHub's generated notes (merged pull requests and contributors
     since the previous release), followed by the install section from
     `.github/release-notes.md` (download, checksum check, first launch, requirements); the
     first-launch steps for unsigned builds are left out when the build is notarized,
   - creates a **draft** release `v0.1.0-beta.2` titled *Whiteprint 0.1.0-beta.2* with the DMG and
     checksum. Versions with a `-`, like `-beta.2`, are marked pre-release.
4. Open the draft under **Releases**, edit the notes, and click **Publish release**.

**Re-runs and duplicate runs.** Runs for the same tag never overlap: a second one waits for the
first. If a release for the tag already exists, the run replaces its DMG and checksum instead of
failing or creating another release; a draft also gets fresh notes, while a published release
keeps the notes it was published with. So re-running a failed or duplicate run is always safe.

The workflow can also be started from the Actions tab (**Release → Run workflow**) with an optional
version (without the `v`); the draft's tag is created when you publish it. Tick *Skip the Finder
window layout* (or set the repository variable `DMG_PLAIN=1`) if Finder scripting misbehaves on the
runner; the DMG then opens as a plain window with the same contents.

To change the install instructions in every release, edit `.github/release-notes.md`. It may use
`{{VERSION}}`, `{{TAG}}`, `{{DMG}}`, `{{SHA256}}` and `{{REPOSITORY}}`; the part between the
`<!-- unsigned -->` markers only appears in builds that aren't notarized.

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

Only the Release workflow uses these; CI and edge builds are always ad-hoc signed, and pull
requests from forks never see secrets. Without them the Release workflow still produces an ad-hoc
signed DMG (see below). Add them under **Settings → Secrets and variables → Actions**:

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
