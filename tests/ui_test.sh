#!/bin/bash
# The program editor and the terminal's keys, tested through the
# simulator: each case is a script of keys and typed lines, and what the
# calculator printed is compared with what it should have. (basic.c's own
# tests, with no screen, are tests/test_basic.c.)
#
#   tests/ui_test.sh        (needs build/sim: make build/sim)
cd "$(dirname "$0")/.."
SIM=build/sim
[ -x $SIM ] || { echo "no $SIM: run make build/sim first"; exit 2; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
failures=0
case_no=0

# run NAME WORD...   what the console printed, in $out, one line each, no CRs
run() {
    case_no=$((case_no + 1))
    name=$1; shift
    rm -rf "$TMP/out" "$TMP/drive"
    out=$(perl -e 'alarm 30; exec @ARGV' $SIM "$TMP/out" "$TMP/drive" "$@" 2>&1 | tr -d '\r' | grep -av ' ms  ')
}
# has LINE   the console printed exactly this line
has() {
    if ! printf '%s\n' "$out" | grep -qxF -- "$1"; then
        echo "FAIL $name: no line \"$1\""; echo "$out" | sed 's/^/    /'; failures=$((failures + 1))
    fi
}
# lacks LINE   it never did
lacks() {
    if printf '%s\n' "$out" | grep -qxF -- "$1"; then
        echo "FAIL $name: there is a line \"$1\""; failures=$((failures + 1))
    fi
}

PROGRAM=(serial:10_REM_DEMO 100 'serial:30_PRINT_"A_LONG_LINE_THAT_WRAPS_ON_THE_SCREEN",I' 100 serial:40_NEXT 100 serial:50_END 100)

# The line text arrives whole: no junk after it (an unterminated edit once stored " E")
run "edit in the middle" "${PROGRAM[@]}" UP UP UP ENTER UP UP UP UP R5C0 R5C2 ENTER ON serial:LIST 100 QUIT
has '10 REM DEMO'
has '30 PRINT "A LONG LINE THAT WRAPS ON THE SCREE2N",I'
has '40 NEXT'
has '50 END'

run "EXIT discards the edit" "${PROGRAM[@]}" UP UP UP ENTER R2C4 R2C4 R2C4 ON ON serial:LIST 100 QUIT
has '30 PRINT "A LONG LINE THAT WRAPS ON THE SCREEN",I'

run "a line edited down to its number is deleted" "${PROGRAM[@]}" UP UP ENTER R2C4 R2C4 R2C4 R2C4 R2C4 ENTER ON serial:LIST 100 QUIT
has '10 REM DEMO'
has '50 END'
lacks '40 NEXT'

run "a line with no number is dropped, not run" "${PROGRAM[@]}" UP UP ENTER R2C4 R2C4 R2C4 R2C4 R2C4 R2C4 R2C4 ENTER ON serial:LIST 100 QUIT
has '40 NEXT'
lacks 'WHAT?'

run "after an edit, the list is still there, on the same line" "${PROGRAM[@]}" UP UP ENTER R5C0 R5C2 ENTER DOWN ON serial:LIST 100 QUIT
has '40 NEXT2'

# The terminal
run "up arrow brings the last line back" serial:PRINT_1 100 serial:PRINT_2 100 $'serial:\x1b[A' 100 QUIT
has '2'

run "ESC O A (another terminal's up arrow)" serial:PRINT_7 100 $'serial:\x1bOA' 100 QUIT
has '7'

run "cursor keys, Home, Delete and End edit the line" $'serial:PRINT_12\x1b[D\x1b[D9' 100 $'serial:PRINT_7\x1b[H??\x1b[3~\x1b[F' 100 QUIT
has '912'
has 'WHAT?'

run "Ctrl-C clears the line at the prompt" $'serial:PRINT_5\x03PRINT_6' 100 QUIT
has '6'
lacks '5'

run "Esc doesn't swallow the next key" $'serial:\x1bxPRINT_3' 100 QUIT
has 'WHAT?'

run "CR LF is one Enter" $'serial:PRINT_1\n' 100 QUIT
n=$(printf '%s\n' "$out" | grep -c '^>$')
[ "$n" -le 1 ] || { echo "FAIL $name: $n empty prompts"; failures=$((failures + 1)); }

# Ctrl-C reaches a running program even with the typed-ahead store full
LONG=$(printf 'x%.0s' $(seq 1 400))
run "Ctrl-C stops a program after a long paste" serial:10_GOTO_10 100 serial:RUN 50 "serial:$LONG" 50 $'serial:\x03' 200 QUIT
has 'BREAK IN 10'

if [ $failures = 0 ]; then
    echo "all $case_no UI cases passed"
else
    echo "$failures FAILED"
    exit 1
fi
