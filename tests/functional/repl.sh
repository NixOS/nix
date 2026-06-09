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
      | grepQuiet -s "error: path \"$testDir/simple.nix\" is not a flake"
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

# Test the `:reload` mechanism with flakes:
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

# Regression: `:reload` on a flake loaded from a *git* work tree must pick up
# uncommitted changes. Guards against the per-process workdir-info cache
# pinning the tree to the rev seen on first load.
if [[ $(type -p git) ]]; then
    createGitRepo gitflake
    cat > gitflake/flake.nix <<EOF
{ outputs = { self }: { changingThing = "beforeChange"; }; }
EOF
    git -C gitflake add flake.nix
    git -C gitflake commit -m init

    rm -f repl_fifo repl_output
    mkfifo repl_fifo
    touch repl_output
    nix repl ./gitflake --experimental-features 'flakes' < repl_fifo >> repl_output 2>&1 &
    repl_pid=$!
    exec 3>repl_fifo
    echo "changingThing" >&3
    for _ in $(seq 1 1000); do
        grep -q "beforeChange" repl_output && break
        sleep 0.1
    done
    grep -q "beforeChange" repl_output || fail "git flake didn't load"
    sed -i 's/beforeChange/afterChange/' gitflake/flake.nix
    echo ":reload" >&3
    echo "changingThing" >&3
    echo "exit" >&3
    exec 3>&-
    wait $repl_pid
    grep -q "afterChange" repl_output || fail ":reload didn't pick up git work tree change"
fi

# Regression: a failed `:lf` must not be remembered for `:reload`,
# and an error in one loaded file must not drop later ones from the reload list.
testReplResponseNoRegex '
:lf ./does-not-exist-flake
:lf ./flake
:r
foo
' '1' \
    --experimental-features 'flakes'

# Don't prompt for more input when getting unexpected EOF in imported files.
testReplResponse "
import $testDir/lang/parse-fail-eof-pos.nix
" \
'.*error: syntax error, unexpected end of file.*'

EDITOR='cat' nix repl <<< ':e derivation' 2>&1 | grepQuiet 'derivationStrict'
EDITOR='cat' nix repl <<< ':e <nix/fetchurl.nix>' 2>&1 | grepQuiet 'builtin:fetchurl'

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

  _NIX_TEST_RAW_MARKDOWN=1 \
  _NIX_TEST_REPL_ECHO=1 \
  nix repl "$@" 2>&1 \
    | stripColors \
    | tr -d '\0' \
    | stripEmptyLinesBeforePrompt \
    | stripFinalPrompt \
    | sed \
      -e "s@$testDir@/path/to/tests/functional@g" \
      -e "s@$testDirNoUnderscores@/path/to/tests/functional@g" \
      -e "s@$nixVersion@<nix version>@g" \
    | grep -vF $'warning: you don\'t have Internet access; disabling some network-dependent features' \
    ;
}

for test in $(cd "$testDir/repl"; echo *.in); do
    test="$(basename "$test" .in)"
    in="$testDir/repl/$test.in"
    actual="$TEST_ROOT/$test.actual"
    expected="$testDir/repl/$test.expected"
    declare -a flags=()
    if test -e "$testDir/repl/$test.flags"; then
      read -r -a flags < "$testDir/repl/$test.flags"
    fi

    # Allow putting comments (lines starting with `# COM:`) in the test for
    # documentation purposes. Regular comments are not skipped, since those are
    # also interpreted by the repl.
    (cd "$testDir/repl"; set +x; runRepl "${flags[@]}" 2>&1) < <(grep -Ev '^[[:space:]]*#[[:space:]]*COM:' "$in") > "$actual" || {
        echo "FAIL: $test (exit code $?)" >&2
        badExitCode=1
    }
    diffAndAcceptInner "$test" "$actual" "$expected"
done

characterisationTestExit
