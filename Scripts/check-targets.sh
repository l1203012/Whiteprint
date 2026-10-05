#!/bin/bash
# Checks that Scripts/targets.sh describes the same module graph as Package.swift:
# the same library and executable targets with the same dependencies, and a
# test target for every Tests/<Module>Tests folder. Needs only bash and python3,
# so it runs on Linux CI runners and on Macs with just the Command Line Tools.
#
#   Scripts/check-targets.sh   prints the differences and exits 1 when they disagree
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
# shellcheck source=Scripts/targets.sh
source Scripts/targets.sh

# One line per target from targets.sh: "<kind> <name> <sorted deps…>".
scripts_graph() {
    local target kind
    for target in $LIBS $EXES; do
        kind=library
        case " $EXES " in *" $target "*) kind=executable ;; esac
        # shellcheck disable=SC2046
        echo "$kind $target" $(deps "$target" | tr ' ' '\n' | sed '/^$/d' | sort)
    done | sort
}

# The same from Package.swift, read as text; it only uses the simple
# .target / .executableTarget / .testTarget(name:dependencies:) forms.
package_graph() {
    python3 - "$1" <<'EOF'
import re, sys
kind_names = {"target": "library", "executableTarget": "executable", "testTarget": "test"}
src = re.sub(r"//[^\n]*", "", open("Package.swift").read())
pattern = re.compile(
    r'\.(target|executableTarget|testTarget)\(\s*name:\s*"([^"]+)"'
    r'(?:\s*,\s*dependencies:\s*\[([^\]]*)\])?')
for kind, name, deps in pattern.findall(src):
    kind = kind_names[kind]
    if (kind == "test") != (sys.argv[1] == "tests"):
        continue
    deps = sorted(re.findall(r'"([^"]+)"', deps or ""))
    print(" ".join([kind, name] + deps) if kind != "test" else name)
EOF
}

status=0
diff_or_fail() {
    local label=$1 left=$2 right=$3 expected=$4 actual=$5
    if [ "$expected" != "$actual" ]; then
        echo "✗ $label differ (< $left, > $right):" >&2
        diff <(echo "$expected") <(echo "$actual") >&2 || true
        status=1
    fi
}

graph=$(package_graph modules | sort)
if [ -z "$graph" ]; then
    echo "✗ no targets found in Package.swift" >&2
    exit 1
fi
diff_or_fail "module targets" Package.swift Scripts/targets.sh "$graph" "$(scripts_graph)"

tests=$(package_graph tests | sort)
folders=$(for dir in Tests/*Tests; do if [ -d "$dir" ]; then basename "$dir"; fi; done | sort)
diff_or_fail "test targets" Package.swift Tests/ "$tests" "$folders"

if [ $status -eq 0 ]; then
    echo "✓ Scripts/targets.sh matches Package.swift ($(echo "$graph" | wc -l | tr -d ' ') modules, $(echo "$tests" | wc -l | tr -d ' ') test targets)"
fi
exit $status
