source common.sh

testDir="$PWD"
cd "$TEST_ROOT"

replCmds="
:bt
:q
"

# replFailingCmds="
# failing = import $testDir/simple-failing.nix
# :b failing
# :log failing
# "

# replUndefinedVariable="
# import $testDir/undefined-variable.nix
# "

echo "nix version: $(nix --version)"

nixArgs=("$@")
na="${nixArgs[@]}"
# na="--extra-experimental-features nix-command"

testRepl () {
    fname=$1
    local replOutput=$(nix eval ${na} --debugger -f $testDir/${fname}.nix <<< "$replCmds")
    # local replOutput=$((nix eval ${na} --debugger -f $testDir/${fname}.nix <<< "$replCmds") 2>&1)
    # echo -e "$replOutput" | grep "^.\[34\;1m[0-9]" > "/home/bburdette/testout/${fname}.txt"
    # local replFiltered=$(echo -e "$replOutput" | { grep "^.\[34\;1m[0-9]" || echo "" ; } | grep -v "file")
    local replFiltered=$(echo -e "$replOutput" | { grep "^.\[34\;1m[0-9]" || echo "" ; } )

    echo -e "$replFiltered" > "/home/bburdette/testout/${fname}.txt"

    local expected="$(cat $testDir/${fname}.expected)"

    ex2=${expected}
    ro2=${replFiltered}

    if [ "${ex2}" != "${ro2}" ]; then
        fail "ex2 output doesn't match for file: $fname"
    fi
    # if [ "${expected}" != "${replOutput}" ]; then
    #     echo "expected: ${expected}"
    #     echo "replOutput: ${replOutput}"

    #     fail "expected output doesn't match"
    # fi
}

# Simple test, try building a drv
testRepl "debugger-attr-bad"
testRepl "debugger-bool-expected"
testRepl "debugger-attr-deep"
testRepl "debugger-break"
testRepl "debugger-attr-missing"