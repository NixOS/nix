source common.sh

testDir="$PWD"
cd "$TEST_ROOT"

replCmds="
:bt
:q
"

echo "nix version: $(nix --version)"

nixArgs=("$@")
na="${nixArgs[@]}"

testRepl () {
    fname=$1
    local replOutput=$(nix eval ${na} --debugger -f $testDir/${fname}.nix <<< "$replCmds")
    local replFiltered=$(echo -e "$replOutput" | { grep "^.\[34\;1m[0-9]" || echo "" ; } )

    echo -e "$replFiltered" > "/home/bburdette/testout/${fname}.txt"

    local expected="$(cat $testDir/${fname}.expected)"

    ex2=${expected}
    ro2=${replFiltered}

    if [ "${ex2}" != "${ro2}" ]; then
        fail "ex2 output doesn't match for file: $fname"
    fi
}

testRepl "debugger-attr-bad"
testRepl "debugger-bool-expected"
testRepl "debugger-attr-deep"
testRepl "debugger-break"
testRepl "debugger-attr-missing"