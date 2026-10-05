#!/bin/bash
# Runs the XCTest suites.
#
#   Scripts/test.sh                     every module that has tests
#   Scripts/test.sh WhiteprintCore …    only these modules
#
# With Xcode installed this is `swift test`. With only the Command Line Tools
# (no XCTest), each suite is compiled into the module it tests together with
# a minimal XCTest stand-in (Scripts/xctest-shim) and run as an executable.
# The stand-in supports XCTestCase with setUp/tearDown, sync/async/throwing
# test methods and the common XCTAssert functions, not expectations.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

if [ $# -eq 0 ]; then
    set -- $(cd Tests && ls -d *Tests 2>/dev/null | sed 's/Tests$//')
fi

if xcrun --find xctest >/dev/null 2>&1; then
    filters=""
    for target in "$@"; do filters="$filters --filter ${target}Tests"; done
    # shellcheck disable=SC2086
    exec swift test $filters
fi

# shellcheck source=Scripts/targets.sh
source Scripts/targets.sh
ARCH=$(uname -m)
OUT=$ROOT/.build/clt/debug/$ARCH
WORK=$ROOT/.build/clt/tests
mkdir -p "$OUT"
status=0

for target in "$@"; do
    [ -d "Tests/${target}Tests" ] || { echo "no tests for $target" >&2; status=1; continue; }
    deps=$(deps "$target")
    # shellcheck disable=SC2086
    [ -z "$deps" ] || Scripts/build.sh $deps >/dev/null
    links=""
    for dep in $deps; do links="$links -l$dep"; done

    dir=$WORK/$target
    rm -rf "$dir"
    mkdir -p "$dir"
    python3 - "$target" "$dir" <<'EOF'
import os, re, sys, glob
target, out = sys.argv[1], sys.argv[2]
calls = []
for path in sorted(glob.glob(f"Tests/{target}Tests/**/*.swift", recursive=True)):
    src = open(path).read()
    # The suite is compiled into the module under test, so drop those imports.
    src = re.sub(rf"^\s*(@testable\s+)?import\s+({target}|XCTest)\s*$", "", src, flags=re.M)
    open(os.path.join(out, os.path.basename(path)), "w").write(src)
    cls = None
    for line in src.splitlines():
        m = re.match(r"\s*(?:final\s+)?class\s+(\w+)\s*:\s*XCTestCase", line)
        if m:
            cls = m.group(1)
        m = re.match(r"\s*func\s+(test\w*)\s*\(\s*\)", line)
        if m and cls:
            calls.append((cls, m.group(1)))
lines = ["import Foundation", "var count = 0"]
for cls, name in calls:
    lines.append(f'await XCTestShimRun("{cls}.{name}") {{ let t = {cls}(); try t.setUpWithError(); t.setUp(); defer {{ t.tearDown() }}; try await t.{name}() }}')
lines.append(f'XCTestShimFinish({len(calls)})')
open(os.path.join(out, "main.swift"), "w").write("\n".join(lines) + "\n")
EOF
    echo "• ${target}Tests"
    # shellcheck disable=SC2046,SC2086
    swiftc -module-name "$target" -Onone -suppress-warnings \
        -target "$ARCH-apple-macos13.0" -I "$OUT" -L "$OUT" $links \
        -o "$dir/run" \
        $(find "Sources/$target" -name '*.swift') "$dir"/*.swift Scripts/xctest-shim/XCTestShim.swift \
        || { status=1; continue; }
    "$dir/run" || status=1
done
exit $status
