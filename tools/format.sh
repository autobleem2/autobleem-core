#!/usr/bin/env bash
# Format the C++ sources with the repo's .clang-format.
#
#   tools/format.sh                  rewrite every tracked source in place
#   tools/format.sh --check          change nothing; list the files that differ, exit 1 if any
#   tools/format.sh [--check] FILE...  just those files
#   tools/format.sh --check --changed [BASE]
#                                    only the files changed against the merge base with BASE
#                                    (default origin/develop) - holds new code to the format
#                                    without a mass reformat of the existing tree
#
# vendored code (third_party) is never touched. CLANG_FORMAT overrides the binary
# (default: clang-format; the autobleem-build image ships clang-format 22).
set -u

check=0
changed=0
given=()
base=origin/develop
while [ $# -gt 0 ]; do
    case "$1" in
        --check) check=1 ;;
        --changed)
            changed=1
            if [ $# -gt 1 ] && [ "${2#--}" = "$2" ]; then base=$2; shift; fi
            ;;
        -*) echo "usage: $0 [--check] [--changed [BASE]] [FILE...]" >&2; exit 2 ;;
        *) given+=("$1") ;;
    esac
    shift
done

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel) || exit 2
cd "$root" || exit 2
cf=${CLANG_FORMAT:-clang-format}
command -v "$cf" >/dev/null || { echo "format.sh: $cf not found" >&2; exit 2; }

if [ ${#given[@]} -gt 0 ]; then
    list=$(printf '%s\n' "${given[@]}")
elif [ "$changed" = 1 ]; then
    mb=$(git merge-base "$base" HEAD) || { echo "format.sh: no merge base with $base" >&2; exit 2; }
    list=$(git diff --name-only --diff-filter=ACMR "$mb" -- '*.cpp' '*.h' '*.hpp' '*.cc')
else
    list=$(git ls-files -- '*.cpp' '*.h' '*.hpp' '*.cc')
fi
files=()
while IFS= read -r f; do
    [ -n "$f" ] || continue
    case "$f" in */third_party/*) continue ;; esac
    [ -f "$f" ] && files+=("$f")
done <<<"$list"

if [ "$check" = 0 ]; then
    [ ${#files[@]} -gt 0 ] && "$cf" -i "${files[@]}"
    echo "format.sh: formatted ${#files[@]} files"
    exit 0
fi

bad=0
lines=0
for f in "${files[@]}"; do
    # one <replacement> element per changed span; counting them gives the size of the diff
    n=$("$cf" --output-replacements-xml "$f" | grep -c '<replacement ')
    if [ "$n" -gt 0 ]; then
        echo "needs formatting: $f ($n replacements)"
        bad=$((bad + 1))
        lines=$((lines + n))
    fi
done
echo "format.sh: $("$cf" --version | head -n1)"
echo "format.sh: checked ${#files[@]} files, $bad need formatting ($lines replacements)"
[ "$bad" = 0 ]
