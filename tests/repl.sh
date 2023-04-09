source common.sh

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

testRepl () {
    local nixArgs=("$@")
    rm -rf repl-result-out || true # cleanup from other runs backed by a foreign nix store
    local replOutput="$(nix repl "${nixArgs[@]}" <<< "$replCmds")"
    echo "$replOutput"
    local outPath=$(echo "$replOutput" |&
        grep -o -E "$NIX_STORE_DIR/\w*-simple")
    nix path-info "${nixArgs[@]}" "$outPath"
    [ "$(realpath ./repl-result-out)" == "$outPath" ] || fail "nix repl :bl doesn't make a symlink"
    # run it again without checking the output to ensure the previously created symlink gets overwritten
    nix repl "${nixArgs[@]}" <<< "$replCmds" || fail "nix repl does not work twice with the same inputs"

    # simple.nix prints a PATH during build
    echo "$replOutput" | grepQuiet -s 'PATH=' || fail "nix repl :log doesn't output logs"
    local replOutput="$(nix repl "${nixArgs[@]}" <<< "$replFailingCmds" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s 'This should fail' \
      || fail "nix repl :log doesn't output logs for a failed derivation"
    local replOutput="$(nix repl --show-trace "${nixArgs[@]}" <<< "$replUndefinedVariable" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s "while evaluating the file" \
      || fail "nix repl --show-trace doesn't show the trace"

    nix repl "${nixArgs[@]}" --option pure-eval true 2>&1 <<< "builtins.currentSystem" \
      | grep "attribute 'currentSystem' missing"
    nix repl "${nixArgs[@]}" 2>&1 <<< "builtins.currentSystem" \
      | grep "$(nix-instantiate --eval -E 'builtins.currentSystem')"
}

# Simple test, try building a drv
testRepl
# Same thing (kind-of), but with a remote store.
testRepl --store "$TEST_ROOT/store?real=$NIX_STORE_DIR"

testReplResponse () {
    local commands="$1"; shift
    local expectedResponse="$1"; shift
    local response="$(nix repl "$@" <<< "$commands")"
    echo "$response" | grepQuiet -s "$expectedResponse" \
      || fail "repl command set:

$commands

does not respond with:

$expectedResponse

but with:

$response"
}

# :a uses the newest version of a symbol
testReplResponse '
:a { a = "1"; }
:a { a = "2"; }
"result: ${a}"
' "result: 2"

# check dollar escaping https://github.com/NixOS/nix/issues/4909
# note the escaped \,
#    \\
# because the second argument is a regex
testReplResponse '
"$" + "{hi}"
' '"\\${hi}"'

testReplResponse '
drvPath
' '".*-simple.drv"' \
$testDir/simple.nix

testReplResponse '
drvPath
' '".*-simple.drv"' \
--file $testDir/simple.nix --experimental-features 'ca-derivations'

testReplResponse '
drvPath
' '".*-simple.drv"' \
--file $testDir/simple.nix --extra-experimental-features 'repl-flake ca-derivations'

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
    ./flake ./flake\#bar --experimental-features 'flakes repl-flake'

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
) | nix repl ./flake --experimental-features 'flakes repl-flake')
echo "$replResult" | grepQuiet -s beforeChange
echo "$replResult" | grepQuiet -s afterChange
