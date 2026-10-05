#!/bin/bash
# Builds a signed, universal Whiteprint.app and the install DMG for a release.
#
#   Scripts/release.sh [version]   .build/release/Whiteprint-<version>.dmg and .dmg.sha256
#
# The version (a leading "v" is dropped) becomes CFBundleShortVersionString;
# it defaults to the one in Resources/Info.plist.
#
#   SIGN_IDENTITY="Developer ID Application: …"   hardened runtime + timestamp (default: ad-hoc)
#   NOTARY_PROFILE=name   notarize and staple with a `notarytool store-credentials` profile,
#   or NOTARY_KEY_PATH, NOTARY_KEY_ID, NOTARY_ISSUER_ID for an App Store Connect API key
#   DMG_PLAIN=1           skip the Finder window layout (icon positions, background view)
#   VOLUME_ICON=path.icns   volume icon (default: Resources/App/AppIcon.icns when present)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${1:-$(plutil -extract CFBundleShortVersionString raw "$ROOT/Resources/Info.plist")}
VERSION=${VERSION#v}
IDENTITY=${SIGN_IDENTITY:--}
VOLUME_NAME=Whiteprint
VOLUME_ICON=${VOLUME_ICON:-$ROOT/Resources/App/AppIcon.icns}
OUT=$ROOT/.build/release
APP=$OUT/Whiteprint.app
DMG=$OUT/Whiteprint-$VERSION.dmg
# Window content size and icon centres; keep in sync with Scripts/make-dmg-background.swift.
WINDOW_WIDTH=640
WINDOW_HEIGHT=400
APP_SPOT="160, 205"
APPLICATIONS_SPOT="480, 205"

step() { echo "• $*"; }

build_app() {
    step "Building universal Whiteprint.app $VERSION"
    CONFIG=release ARCHS="arm64 x86_64" SIGN_IDENTITY=- "$ROOT/Scripts/build.sh" app
    rm -rf "$OUT"
    mkdir -p "$OUT"
    ditto "$ROOT/.build/Whiteprint.app" "$APP"
    plutil -replace CFBundleShortVersionString -string "$VERSION" "$APP/Contents/Info.plist"
}

# The helper first, then the bundle that seals it.
sign_app() {
    local options=()
    if [ "$IDENTITY" = "-" ]; then
        step "Signing ad-hoc (set SIGN_IDENTITY for a Developer ID signature)"
    else
        step "Signing with $IDENTITY"
        options=(--options runtime --timestamp)
    fi
    # bash 3.2 treats an empty array as unset under `set -u`.
    codesign --force ${options[@]+"${options[@]}"} --sign "$IDENTITY" "$APP/Contents/MacOS/whiteprint-mcp"
    codesign --force ${options[@]+"${options[@]}"} --sign "$IDENTITY" "$APP"
    codesign --verify --deep --strict "$APP"
}

stage_volume() {
    local stage=$1
    mkdir -p "$stage/.background"
    ditto "$APP" "$stage/Whiteprint.app"
    ln -s /Applications "$stage/Applications"
    tiffutil -cathidpicheck "$ROOT/Resources/DMG/background.png" "$ROOT/Resources/DMG/background@2x.png" \
        -out "$stage/.background/background.tiff" >/dev/null 2>&1
}

# Sets the window size, icon positions and background through Finder, which
# writes them to the volume's .DS_Store. Fails when Finder can't be scripted.
layout_window() {
    local left=200 top=120
    # A hidden toolbar still leaves the title bar inside the window bounds.
    local right=$((left + WINDOW_WIDTH)) bottom=$((top + WINDOW_HEIGHT + 28))
    # perl's alarm stands in for `timeout`, so an unanswered automation prompt can't hang the build.
    perl -e 'alarm shift; exec @ARGV' 120 osascript >/dev/null <<EOF
tell application "Finder"
    tell disk "$VOLUME_NAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {$left, $top, $right, $bottom}
        set options to the icon view options of container window
        set arrangement of options to not arranged
        set icon size of options to 128
        set text size of options to 13
        set background picture of options to file ".background:background.tiff"
        set position of item "Whiteprint.app" of container window to {$APP_SPOT}
        set position of item "Applications" of container window to {$APPLICATIONS_SPOT}
        update without registering applications
        delay 1
        close
        open
        delay 1
        close
    end tell
end tell
EOF
}

# After the Finder layout, since Finder drops a volume icon that is already there.
set_volume_icon() {
    local volume=$1
    if [ ! -f "$VOLUME_ICON" ]; then
        echo "  no volume icon ($VOLUME_ICON not found)"
        return 0
    fi
    cp "$VOLUME_ICON" "$volume/.VolumeIcon.icns"
    if command -v SetFile >/dev/null; then
        SetFile -a C "$volume"
    else
        # The kHasCustomIcon Finder flag, byte 8 of FinderInfo.
        xattr -wx com.apple.FinderInfo "0000000000000000040000000000000000000000000000000000000000000000" "$volume"
    fi
}

detach() {
    local device=$1 attempt
    for attempt in 1 2 3 4 5; do
        hdiutil detach "$device" -quiet && return 0
        sleep 2
    done
    hdiutil detach "$device" -force -quiet
}

make_dmg() {
    step "Building $(basename "$DMG")"
    local work stage scratch device volume
    work=$(mktemp -d "${TMPDIR:-/tmp}/whiteprint-dmg.XXXXXX")
    stage=$work/stage
    scratch=$work/scratch.dmg
    stage_volume "$stage"
    hdiutil create -quiet -volname "$VOLUME_NAME" -fs HFS+ -srcfolder "$stage" -format UDRW -size 200m -ov "$scratch"

    # Finder finds the volume by name, so another "Whiteprint" volume gets in the way.
    if [ -e "/Volumes/$VOLUME_NAME" ] && [ "${DMG_PLAIN:-0}" != 1 ]; then
        echo "  /Volumes/$VOLUME_NAME is already mounted; eject it to lay out the window" >&2
        DMG_PLAIN=1
    fi
    local browse=()
    [ "${DMG_PLAIN:-0}" = 1 ] && browse=(-nobrowse)
    local mounted
    mounted=$(hdiutil attach -readwrite -noverify -noautoopen ${browse[@]+"${browse[@]}"} "$scratch" | grep Apple_HFS)
    device=$(echo "$mounted" | awk '{ print $1 }')
    volume=$(echo "$mounted" | awk -F '\t' '{ print $NF }')
    if [ "${DMG_PLAIN:-0}" = 1 ]; then
        echo "  plain window layout (DMG_PLAIN=1)"
    elif layout_window; then
        # Finder writes .DS_Store asynchronously.
        local i
        for i in $(seq 1 20); do
            [ -s "$volume/.DS_Store" ] && break
            sleep 0.5
        done
        [ -s "$volume/.DS_Store" ] || echo "  Finder did not write .DS_Store; the window keeps its default layout" >&2
    else
        echo "  Finder could not be scripted (no automation permission?); plain window layout" >&2
    fi
    set_volume_icon "$volume"
    # Opens the window when the volume is mounted; not supported on every Mac.
    bless --folder "$volume" --openfolder "$volume" 2>/dev/null || true
    rm -rf "$volume/.fseventsd"
    chmod -Rf go-w "$volume" 2>/dev/null || true
    sync
    detach "$device"

    rm -f "$DMG"
    hdiutil convert -quiet "$scratch" -format UDZO -imagekey zlib-level=9 -o "$DMG"
    rm -rf "$work"
    if [ "$IDENTITY" != "-" ]; then
        codesign --force --timestamp --sign "$IDENTITY" "$DMG"
    fi
}

notarize() {
    local credentials=()
    if [ -n "${NOTARY_PROFILE:-}" ]; then
        credentials=(--keychain-profile "$NOTARY_PROFILE")
    elif [ -n "${NOTARY_KEY_PATH:-}" ]; then
        credentials=(--key "$NOTARY_KEY_PATH" --key-id "${NOTARY_KEY_ID:?}" --issuer "${NOTARY_ISSUER_ID:?}")
    else
        step "Not notarizing (set NOTARY_PROFILE or NOTARY_KEY_PATH to notarize)"
        return 0
    fi
    if [ "$IDENTITY" = "-" ]; then
        echo "Notarization needs a Developer ID signature; set SIGN_IDENTITY." >&2
        exit 1
    fi
    if ! xcrun --find notarytool >/dev/null 2>&1 || ! xcrun --find stapler >/dev/null 2>&1; then
        step "Skipping notarization: notarytool and stapler need Xcode or newer Command Line Tools"
        return 0
    fi
    step "Notarizing (this can take a few minutes)"
    xcrun notarytool submit "$DMG" "${credentials[@]}" --wait
    xcrun stapler staple "$DMG"
}

checksum() {
    (cd "$OUT" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")
}

build_app
sign_app
make_dmg
notarize
checksum
echo "✓ $DMG"
echo "  $(cut -d' ' -f1 < "$DMG.sha256")  sha256"
