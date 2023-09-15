source common.sh

testDir="$PWD"
cd "$TEST_ROOT"

replCmds="
:bt
:q
"

# here we're testing the sequence and content of backtraces.
# - tests should trigger all the places DebugTraces are made.
#    these are (currently):
#        EvalState::cacheFile:
#            "while evaluating the file '%1%':"
#        ExprSelect::eval:
#            "while evaluating the attribute '%1%'"
#        EvalState::callFunction:
#            "while calling %s"
#        EvalState::forceValueDeep:
#            "while deep evaluating the attribute '%1%'"
#        runDebugRepl:
#            in error handling.
#        runDebugRepl:
#            in primops_break
# - backtrace sequence is as expeected.
# - no extra traces, all expected traces are present.


echo "nix version: $(nix --version)"

nixArgs=("$@")
na="${nixArgs[@]}"

testRepl () {
    fname=$1
    local replOutput=$(nix eval ${na} --debugger -f $testDir/${fname}.nix <<< "$replCmds")
    # grep: only include backtrace sequence lines, not hex position or code location.
    # sed: remove file path as that will vary.
    local replFiltered=$(echo -e "$replOutput" | { grep "^.\[34\;1m[0-9]" | sed 's/while evaluating the file.*/while evaluating the file/' || echo "" ; }  )

    # write the output to a file for debug
    # echo -e "$replFiltered" > "/home/bburdette/testout/${fname}.txt"

    local expected="$(cat $testDir/${fname}.expected)"

    ex2=${expected}
    ro2=${replFiltered}

    if [ "${ex2}" != "${ro2}" ]; then
        fail "backtrace output doesn't match for file: $fname"
    fi
}

testRepl "debugger-attr-bad"
testRepl "debugger-bool-expected"
testRepl "debugger-attr-deep"
testRepl "debugger-break"
