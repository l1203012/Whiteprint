#!/bin/bash
# Builds Whiteprint with plain swiftc, so it works with only the Command Line
# Tools installed (SwiftPM needs Xcode for its XCTest lookup).
#
#   Scripts/build.sh                    build every module and both executables
#   Scripts/build.sh WhiteprintRender   build one module and what it depends on
#   Scripts/build.sh app                build .build/Whiteprint.app
#   Scripts/build.sh dmg [version]      same as Scripts/release.sh (see docs/RELEASING.md)
#
#   CONFIG=release   optimised build (default: debug)
#   ARCHS="arm64 x86_64"   architectures (default: this Mac; release: both)
#   SIGN_IDENTITY="Developer ID Application: …"   (default: ad-hoc "-")
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONFIG=${CONFIG:-debug}
if [ "$CONFIG" = release ]; then
    ARCHS=${ARCHS:-"arm64 x86_64"}
    OPT="-O -whole-module-optimization"
else
    ARCHS=${ARCHS:-$(uname -m)}
    OPT="-Onone -g"
fi
OUT=$ROOT/.build/clt/$CONFIG
MIN_MACOS=13.0
# shellcheck source=Scripts/targets.sh
source "$ROOT/Scripts/targets.sh"

module_name() { echo "$1" | tr '-' '_'; }
sources() { find "$ROOT/Sources/$1" -name '*.swift' | sort; }

# Rebuild only when a source or dependency is newer than the output.
up_to_date() {
    local output=$1 target=$2
    [ -e "$output" ] || return 1
    [ -z "$(find "$ROOT/Sources/$target" -name '*.swift' -newer "$output" | head -1)" ] || return 1
    local dep
    for dep in $(deps "$target"); do
        [ "$OUT/$ARCH/lib$dep.a" -nt "$output" ] && return 1
    done
    return 0
}

build_lib() {
    local target=$1 dir=$OUT/$ARCH dep
    for dep in $(deps "$target"); do build_lib "$dep"; done
    up_to_date "$dir/lib$target.a" "$target" && return 0
    echo "• $target ($ARCH)"
    mkdir -p "$dir"
    # shellcheck disable=SC2046,SC2086
    swiftc -parse-as-library -module-name "$target" $OPT \
        -target "$ARCH-apple-macos$MIN_MACOS" -I "$dir" \
        -emit-module -emit-module-path "$dir/$target.swiftmodule" \
        -emit-library -static -o "$dir/lib$target.a" \
        $(sources "$target")
}

build_exe() {
    local target=$1 dir=$OUT/$ARCH dep links="" parse=""
    for dep in $(deps "$target"); do
        build_lib "$dep"
        links="$links -l$dep"
    done
    up_to_date "$dir/$target" "$target" && return 0
    echo "• $target ($ARCH)"
    # A target with main.swift uses top-level code; otherwise it has an @main type.
    [ -e "$ROOT/Sources/$target/main.swift" ] || parse="-parse-as-library"
    # shellcheck disable=SC2046,SC2086
    swiftc $parse -module-name "$(module_name "$target")" $OPT \
        -target "$ARCH-apple-macos$MIN_MACOS" -I "$dir" -L "$dir" $links \
        -o "$dir/$target" $(sources "$target")
}

build() {
    local target
    for ARCH in $ARCHS; do
        for target in "$@"; do
            case " $EXES " in
                *" $target "*) build_exe "$target" ;;
                *) build_lib "$target" ;;
            esac
        done
    done
}

# Merges the per-architecture builds of an executable into one binary.
universal() {
    local target=$1 dest=$2 inputs="" arch
    for arch in $ARCHS; do inputs="$inputs $OUT/$arch/$target"; done
    # shellcheck disable=SC2086
    lipo -create $inputs -output "$dest"
}

bundle() {
    build $EXES
    local app=$ROOT/.build/Whiteprint.app
    rm -rf "$app"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
    cp "$ROOT/Resources/Info.plist" "$app/Contents/Info.plist"
    if [ -d "$ROOT/Resources/App" ]; then
        cp -R "$ROOT/Resources/App/." "$app/Contents/Resources/"
    fi
    universal WhiteprintApp "$app/Contents/MacOS/Whiteprint"
    universal whiteprint-mcp "$app/Contents/MacOS/whiteprint-mcp"
    local build_number
    build_number=$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)
    plutil -replace CFBundleVersion -string "$build_number" "$app/Contents/Info.plist"
    local identity=${SIGN_IDENTITY:--}
    local runtime=""
    [ "$identity" = "-" ] || runtime="--options runtime --timestamp"
    # shellcheck disable=SC2086
    codesign --force $runtime --sign "$identity" "$app/Contents/MacOS/whiteprint-mcp"
    # shellcheck disable=SC2086
    codesign --force $runtime --sign "$identity" "$app"
    echo "✓ $app"
}

case "${1:-all}" in
    all) build $LIBS $EXES ;;
    app) bundle ;;
    dmg) shift; exec "$ROOT/Scripts/release.sh" "$@" ;;
    *) build "$@" ;;
esac
