#!/usr/bin/env bash

source common.sh
source characterisation/framework.sh

testDir="$PWD"
cd "$TEST_ROOT"

replCmds="
simple = 1
simple = import $testDir/simple.nix
:bl simple
:log simple
"

replFailingCmds="
failing = import $testDir/simple-failing.nix
:b failing
:log failing
"

replUndefinedVariable="
import $testDir/undefined-variable.nix
"

TODO_NixOS

testRepl () {
    local nixArgs
    nixArgs=("$@")
    rm -rf repl-result-out || true # cleanup from other runs backed by a foreign nix store
    local replOutput
    replOutput="$(nix repl "${nixArgs[@]}" <<< "$replCmds")"
    echo "$replOutput"
    local outPath
    outPath=$(echo "$replOutput" |&
        grep -o -E "$NIX_STORE_DIR/\w*-simple")
    nix path-info "${nixArgs[@]}" "$outPath"
    [ "$(realpath ./repl-result-out)" == "$outPath" ] || fail "nix repl :bl doesn't make a symlink"
    # run it again without checking the output to ensure the previously created symlink gets overwritten
    nix repl "${nixArgs[@]}" <<< "$replCmds" || fail "nix repl does not work twice with the same inputs"

    # simple.nix prints a PATH during build
    echo "$replOutput" | grepQuiet -s 'PATH=' || fail "nix repl :log doesn't output logs"
    replOutput="$(nix repl "${nixArgs[@]}" <<< "$replFailingCmds" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s 'This should fail' \
      || fail "nix repl :log doesn't output logs for a failed derivation"
    replOutput="$(nix repl --show-trace "${nixArgs[@]}" <<< "$replUndefinedVariable" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s "while evaluating the file" \
      || fail "nix repl --show-trace doesn't show the trace"

    nix repl "${nixArgs[@]}" --option pure-eval true 2>&1 <<< "builtins.currentSystem" \
      | grep "attribute 'currentSystem' missing"
    nix repl "${nixArgs[@]}" 2>&1 <<< "builtins.currentSystem" \
      | grep "$(nix-instantiate --eval -E 'builtins.currentSystem')"

    expectStderr 1 nix repl "${testDir}/simple.nix" \
      | grepQuiet -s "error: path '$testDir/simple.nix' is not a flake"
}

# Simple test, try building a drv
testRepl
# Same thing (kind-of), but with a remote store.
testRepl --store "$TEST_ROOT/store?real=$NIX_STORE_DIR"

# Remove ANSI escape sequences. They can prevent grep from finding a match.
stripColors () {
    sed -E 's/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g'
}

testReplResponseGeneral () {
    local grepMode commands expectedResponse response
    grepMode="$1"; shift
    commands="$1"; shift
    expectedResponse="$1"; shift
    response="$(nix repl "$@" <<< "$commands" | stripColors)"
    echo "$response" | grepQuiet "$grepMode" -s "$expectedResponse" \
      || fail "repl command set:

$commands

does not respond with:

$expectedResponse

but with:

$response
"
}

testReplResponse () {
    testReplResponseGeneral --basic-regexp "$@"
}

testReplResponseNoRegex () {
    testReplResponseGeneral --fixed-strings "$@"
}

# :a uses the newest version of a symbol
#
# shellcheck disable=SC2016
testReplResponse '
:a { a = "1"; }
:a { a = "2"; }
"result: ${a}"
' "result: 2"

# check dollar escaping https://github.com/NixOS/nix/issues/4909
# note the escaped \,
#    \\
# because the second argument is a regex
#
# shellcheck disable=SC2016
testReplResponseNoRegex '
"$" + "{hi}"
' '"\${hi}"'

testReplResponse '
drvPath
' '".*-simple.drv"' \
--file "$testDir/simple.nix"

testReplResponse '
drvPath
' '".*-simple.drv"' \
--file "$testDir/simple.nix" --experimental-features 'ca-derivations'

mkdir -p flake && cat <<EOF > flake/flake.nix
{
    outputs = { self }: {
        foo = 1;
        bar.baz = 2;

        changingThing = "beforeChange";
    };
}
EOF
testReplResponse '
foo + baz
' "3" \
    ./flake ./flake\#bar --experimental-features 'flakes'

# Test the `:reload` mechansim with flakes:
# - Eval `./flake#changingThing`
# - Modify the flake
# - Re-eval it
# - Check that the result has changed
replResult=$( (
echo "changingThing"
sleep 1 # Leave the repl the time to eval 'foo'
sed -i 's/beforeChange/afterChange/' flake/flake.nix
echo ":reload"
echo "changingThing"
) | nix repl ./flake --experimental-features 'flakes')
echo "$replResult" | grepQuiet -s beforeChange
echo "$replResult" | grepQuiet -s afterChange

# Test recursive printing and formatting
# Normal output should print attributes in lexicographical order non-recursively
testReplResponseNoRegex '
{ a = { b = 2; }; l = [ 1 2 3 ]; s = "string"; n = 1234; x = rec { y = { z = { inherit y; }; }; }; }
' \
'{
  a = { ... };
  l = [ ... ];
  n = 1234;
  s = "string";
  x = { ... };
}
'

# Same for lists, but order is preserved
testReplResponseNoRegex '
[ 42 1 "thingy" ({ a = 1; }) ([ 1 2 3 ]) ]
' \
'[
  42
  1
  "thingy"
  { ... }
  [ ... ]
]
'

# Same for let expressions
testReplResponseNoRegex '
let x = { y = { a = 1; }; inherit x; }; in x
' \
'{
  x = { ... };
  y = { ... };
}
'

# The :p command should recursively print sets, but prevent infinite recursion
testReplResponseNoRegex '
:p { a = { b = 2; }; s = "string"; n = 1234; x = rec { y = { z = { inherit y; }; }; }; }
' \
'{
  a = { b = 2; };
  n = 1234;
  s = "string";
  x = {
    y = {
      z = {
        y = «repeated»;
      };
    };
  };
}
'

# Same for lists
testReplResponseNoRegex '
:p [ 42 1 "thingy" (rec { a = 1; b = { inherit a; inherit b; }; }) ([ 1 2 3 ]) ]
' \
'[
  42
  1
  "thingy"
  {
    a = 1;
    b = {
      a = 1;
      b = «repeated»;
    };
  }
  [
    1
    2
    3
  ]
]
'

# Same for let expressions
testReplResponseNoRegex '
:p let x = { y = { a = 1; }; inherit x; }; in x
' \
'{
  x = «repeated»;
  y = { a = 1 };
}
'

# TODO: move init to characterisation/framework.sh
badDiff=0
badExitCode=0

nixVersion="$(nix eval --impure --raw --expr 'builtins.nixVersion' --extra-experimental-features nix-command)"

runRepl () {
  # TODO: pass arguments to nix repl; see lang.sh
  nix repl 2>&1 \
    | stripColors \
    | sed \
      -e "s@$testDir@/path/to/tests/functional@g" \
      -e "s@$nixVersion@<nix version>@g" \
      -e "s@Added [0-9]* variables@Added <number omitted> variables@g" \
    | grep -vF $'warning: you don\'t have Internet access; disabling some network-dependent features' \
    ;
}

for test in $(cd "$testDir/repl"; echo *.in); do
    test="$(basename "$test" .in)"
    in="$testDir/repl/$test.in"
    actual="$testDir/repl/$test.actual"
    expected="$testDir/repl/$test.expected"
    (cd "$testDir/repl"; set +x; runRepl 2>&1) < "$in" > "$actual"
    diffAndAcceptInner "$test" "$actual" "$expected"
done

characterisationTestExit
