set -e
#set -x

LUMAIL="$1"

[ -x "$LUMAIL" ]

PASSES=0
FAILURES=0

run_rc() {
    local rc="$1"
    local stdoutfile="output/${rc}.stdout"
    local expectfile="${rc}.expected"
    local result

    set +e  # Temporarily allow errors (which we'll capture).
      OUTFILE="${stdoutfile}" "${LUMAIL}" --nodefault --rcfile "testsetup.lua" --rcfile "${rc}" --eval "exit()"
      result="$?"
    set -e

    echo "Exit: $result" >> "${stdoutfile}"
    # If it didn't exit nicely, reset the terminal
    if [ $result -ne 0 ]; then
        stty sane ; reset
    fi

    set +e
    diff -u "${expectfile}" "${stdoutfile}"
    result="$?"
    set -e
    if [ $result -ne 0 ]; then
        FAILURES=$((FAILURES+1))
    else
        PASSES=$((PASSES+1))
    fi
}

for rc in *.rc ; do
    run_rc "$rc"
done

echo "PASSES: $PASSES"
echo "FAILURES: $FAILURES"
[ "$FAILURES" -eq 0 ] || exit 1