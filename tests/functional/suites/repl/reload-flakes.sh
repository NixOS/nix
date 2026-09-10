# shellcheck shell=bash

# shellcheck disable=SC2164
cd "$TEST_ROOT"

# Doesn't do much without https://github.com/NixOS/nix/issues/10069
enableFeatures 'flakes'

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
' "3" ./flake ./flake\#bar

# Test the `:reload` mechanism with flakes:
# - Eval `./flake#changingThing`
# - Modify the flake
# - Re-eval it
# - Check that the result has changed

mkfifo repl_fifo
touch repl_output
nix repl ./flake < repl_fifo >> repl_output 2>&1 &
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
    nix repl ./gitflake < repl_fifo >> repl_output 2>&1 &
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
' '1'
