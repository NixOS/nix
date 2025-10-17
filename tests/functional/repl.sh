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

# FIXME: repl tests fail on systems with stack limits
stack_ulimit="$(ulimit -Hs)"
stack_required="$((64 * 1024 * 1024))"
if [[ "$stack_ulimit" != "unlimited" ]]; then
    ((stack_ulimit < stack_required)) && skipTest "repl tests cannot run on systems with stack size <$stack_required ($stack_ulimit)"
fi

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

    # regression test for #12163
    replOutput=$(nix repl "${nixArgs[@]}" 2>&1 <<< ":sh import $testDir/simple.nix")
    echo "$replOutput" | grepInverse "error: Cannot run 'nix-shell'"

    expectStderr 1 nix repl "${testDir}/simple.nix" \
      | grepQuiet -s "error: path '$testDir/simple.nix' is not a flake"
}

# Simple test, try building a drv
testRepl
# Same thing (kind-of), but with a remote store.
testRepl --store "$TEST_ROOT/other-root?real=$NIX_STORE_DIR"

# Remove ANSI escape sequences. They can prevent grep from finding a match.
stripColors () {
    sed -E 's/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g'
}

testReplResponseGeneral () {
    local grepMode commands expectedResponse response
    grepMode="$1"; shift
    commands="$1"; shift
    # Expected response can contain newlines.
    # grep can't handle multiline patterns, so replace newlines with TEST_NEWLINE
    # in both expectedResponse and response.
    # awk ORS always adds a trailing record separator, so we strip it with sed.
    expectedResponse="$(printf '%s' "$1" | awk 1 ORS=TEST_NEWLINE | sed 's/TEST_NEWLINE$//')"; shift
    # We don't need to strip trailing record separator here, since extra data is ok.
    response="$(nix repl "$@" <<< "$commands" 2>&1 | stripColors | awk 1 ORS=TEST_NEWLINE)"
    printf '%s' "$response" | grepQuiet "$grepMode" -s "$expectedResponse" \
      || fail "$(echo "repl command set:

$commands

does not respond with:

---
$expectedResponse
---

but with:

---
$response
---

" | sed 's/TEST_NEWLINE/\n/g')"
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

testReplResponse $'
:a { a = 1; b = 2; longerName = 3; "with spaces" = 4; }
' 'Added 4 variables.
a, b, longerName, "with spaces"
'

cat <<EOF > attribute-set.nix
{
    a = 1;
    b = 2;
    longerName = 3;
    "with spaces" = 4;
}
EOF
testReplResponse '
:l ./attribute-set.nix
' 'Added 4 variables.
a, b, longerName, "with spaces"
'

testReplResponseNoRegex $'
:a builtins.foldl\' (x: y: x // y) {} (map (x: { ${builtins.toString x} = x; }) (builtins.genList (x: x) 23))
' 'Added 23 variables.
"0", "1", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "2", "20", "21", "22", "3", "4", "5", "6"
... and 3 more; view with :ll'

# Test the `:reload` mechansim with flakes:
# - Eval `./flake#changingThing`
# - Modify the flake
# - Re-eval it
# - Check that the result has changed
mkfifo repl_fifo
touch repl_output
nix repl ./flake --experimental-features 'flakes' < repl_fifo >> repl_output 2>&1 &
repl_pid=$!
exec 3>repl_fifo # Open fifo for writing
echo "changingThing" >&3
for i in $(seq 1 1000); do
    if grep -q "beforeChange" repl_output; then
        break
    fi
    cat repl_output
    sleep 0.1
done
if [[ "$i" -eq 100 ]]; then
    echo "Timed out waiting for beforeChange"
    exit 1
fi

sed -i 's/beforeChange/afterChange/' flake/flake.nix

# Send reload and second command
echo ":reload" >&3
echo "changingThing" >&3
echo "exit" >&3
exec 3>&- # Close fifo
wait $repl_pid # Wait for process to finish
grep -q "afterChange" repl_output

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
  x = «repeated»;
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
  y = { a = 1; };
}
'

# Don't prompt for more input when getting unexpected EOF in imported files.
testReplResponse "
import $testDir/lang/parse-fail-eof-pos.nix
" \
'.*error: syntax error, unexpected end of file.*'

# TODO: move init to characterisation/framework.sh
badDiff=0
badExitCode=0

nixVersion="$(nix eval --impure --raw --expr 'builtins.nixVersion' --extra-experimental-features nix-command)"

# TODO: write a repl interacter for testing. Papering over the differences between readline / editline and between platforms is a pain.

# I couldn't get readline and editline to agree on the newline before the prompt,
# so let's just force it to be one empty line.
stripEmptyLinesBeforePrompt() {
  # --null-data:  treat input as NUL-terminated instead of newline-terminated
  sed --null-data 's/\n\n*nix-repl>/\n\nnix-repl>/g'
}

# We don't get a final prompt on darwin, so we strip this as well.
stripFinalPrompt() {
  # Strip the final prompt and/or any trailing spaces
  sed --null-data \
    -e 's/\(.*[^\n]\)\n\n*nix-repl>[ \n]*$/\1/' \
    -e 's/[ \n]*$/\n/'
}

runRepl () {

  # That is right, we are also filtering out the testdir _without underscores_.
  # This is crazy, but without it, GHA will fail to run the tests, showing paths
  # _with_ underscores in the set -x log, but _without_ underscores in the
  # supposed nix repl output. I have looked in a number of places, but I cannot
  # find a mechanism that could cause this to happen.
  local testDirNoUnderscores
  testDirNoUnderscores="${testDir//_/}"

  # TODO: pass arguments to nix repl; see lang.sh
  _NIX_TEST_RAW_MARKDOWN=1 \
  _NIX_TEST_REPL_ECHO=1 \
  nix repl 2>&1 \
    | stripColors \
    | tr -d '\0' \
    | stripEmptyLinesBeforePrompt \
    | stripFinalPrompt \
    | sed \
      -e "s@$testDir@/path/to/tests/functional@g" \
      -e "s@$testDirNoUnderscores@/path/to/tests/functional@g" \
      -e "s@$nixVersion@<nix version>@g" \
      -e "/Added [0-9]* variables/{s@ [0-9]* @ <number omitted> @;n;d}" \
      -e '/\.\.\. and [0-9]* more; view with :ll/d' \
    | grep -vF $'warning: you don\'t have Internet access; disabling some network-dependent features' \
    ;
}

for test in $(cd "$testDir/repl"; echo *.in); do
    test="$(basename "$test" .in)"
    in="$testDir/repl/$test.in"
    actual="$TEST_ROOT/$test.actual"
    expected="$testDir/repl/$test.expected"
    (cd "$testDir/repl"; set +x; runRepl 2>&1) < "$in" > "$actual" || {
        echo "FAIL: $test (exit code $?)" >&2
        badExitCode=1
    }
    diffAndAcceptInner "$test" "$actual" "$expected"
done

characterisationTestExit
