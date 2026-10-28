#!/usr/bin/env sh
# Segment the existing deterministic scenarios for the Dreamcast PVR report.
# The ranges are the input-script transitions, not an inferred game-state API.
# ponytail: use a visual/state marker if a future scenario needs sub-screen cuts.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
SCENARIO="${1:-race_basic}"
[ "$#" -eq 0 ] || shift
[ -x "$ROOT/build/m2segment" ] || { echo "build/m2segment is missing; run scripts/setup.py with your own ROM set first" >&2; exit 2; }
if [ "$SCENARIO" = attract ]; then
    INPUT=""
    FRAMES=600
else
    INPUT="$ROOT/scripts/inputs/$SCENARIO.txt"
    [ -f "$INPUT" ] || { echo "unknown scenario: $SCENARIO" >&2; exit 2; }
    FRAMES="$(awk '/^frames/{print $2; exit}' "$INPUT")"
    [ -n "$FRAMES" ] || { echo "no frame count in $INPUT" >&2; exit 2; }
fi

set -- "$ROOT/build/m2segment" "$ROOT/build/rom_cache/daytona93" "$FRAMES" --stage boot_title_attract:0-1199 "$@"
[ -z "$INPUT" ] || set -- "$@" --inputs "$INPUT"
case "$SCENARIO" in
    attract)
        ;;
    race_basic)
        set -- "$@" --stage credit:1200-1399 --stage selects:1400-2399 --stage beginner_race:2400-5999
        ;;
    course_advanced)
        set -- "$@" --stage credit:1200-1399 --stage selects:1400-1949 --stage advanced_race:1950-5999
        ;;
    course_expert)
        set -- "$@" --stage credit:1200-1399 --stage selects:1400-1989 --stage expert_race:1990-5999
        ;;
    time_attack)
        set -- "$@" --stage credit:1200-1399 --stage selects:1400-1849 --stage time_attack:1850-5999
        ;;
    race_steer_left|race_steer_right)
        set -- "$@" --stage credit:1200-1399 --stage selects:1400-2499 --stage race:2500-5999
        ;;
    test_mode|test_drive|test_input|test_memory|test_tgp)
        set -- "$@" --stage test_entry:1200-1499 --stage test_menu_or_test:1500-"$((FRAMES - 1))"
        ;;
    *)
        echo "no stage map for $SCENARIO" >&2
        exit 2
        ;;
esac
exec "$@"
