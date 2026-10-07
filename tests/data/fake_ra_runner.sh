#!/bin/sh
# A FAKE RetroArch job runner for the tests (RaJobService) and for looking at the launcher's panels: it follows the
# runner contract in core/services/ra_job_service.h without touching anything but its progress file.
#
#   fake_ra_runner.sh install|update|remove [--bios] --progress FILE
#
# Environment (all optional):
#   FAKE_RA_DELAY   seconds between two progress lines (default 0.2; fractions need a sleep that takes them)
#   FAKE_RA_TICKS   progress lines per phase (default 5)
#   FAKE_RA_FAIL    exit with this code at the phase named in FAKE_RA_FAIL_AT (default 2), after writing why to stdout
#   FAKE_RA_FAIL_AT the phase (1-based) the failure happens in (default 2)
# SIGTERM: the state is "resumable" by definition here - it says so on stdout and exits 5, within a tick.

action="$1"
shift
bios=""
progress=""
while [ $# -gt 0 ]; do
    case "$1" in
    --bios) bios=1 ;;
    --progress)
        shift
        progress="$1"
        ;;
    esac
    shift
done
[ -n "$progress" ] || {
    echo "no --progress" >&2
    exit 1
}

delay="${FAKE_RA_DELAY:-0.2}"
ticks="${FAKE_RA_TICKS:-5}"
failAt="${FAKE_RA_FAIL_AT:-2}"

trap 'echo "stopped - the next start carries on"; exit 5' TERM

case "$action" in
install | update) titles="Downloading RetroArch|Installing RetroArch|Downloading cores|Finishing" ;;
remove)
    titles="Removing RetroArch"
    [ -n "$bios" ] && titles="Removing RetroArch|Removing BIOS files"
    ;;
*)
    echo "unknown action $action" >&2
    exit 1
    ;;
esac

n=$(echo "$titles" | tr '|' '\n' | wc -l)
i=0
echo "$action: $n phases"
OLDIFS="$IFS"
IFS='|'
for title in $titles; do
    IFS="$OLDIFS"
    i=$((i + 1))
    t=0
    total=$((ticks * 1000000))
    while [ "$t" -le "$ticks" ]; do
        echo "phase $i/$n|$title|$((t * 1000000))|$total" >"$progress.tmp"
        mv "$progress.tmp" "$progress"
        echo "$title $t/$ticks"
        if [ -n "$FAKE_RA_FAIL" ] && [ "$i" -eq "$failAt" ] && [ "$t" -ge 2 ]; then
            echo "fake failure in $title"
            exit "$FAKE_RA_FAIL"
        fi
        t=$((t + 1))
        sleep "$delay" &
        wait $!
    done
    IFS='|'
done
IFS="$OLDIFS"
echo "done"
exit 0
