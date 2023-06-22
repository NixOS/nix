source common.sh

export TEST_VAR=foo # for eval-okay-getenv.nix
export NIX_REMOTE=dummy://
export NIX_STORE_DIR=/nix/store

nix-instantiate --eval -E 'builtins.trace "Hello" 123' 2>&1 | grepQuiet Hello
nix-instantiate --eval -E 'builtins.trace "Hello" 123' 2>/dev/null | grepQuiet 123
nix-instantiate --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1
nix-instantiate --trace-verbose --eval -E 'builtins.traceVerbose "Hello" 123' 2>&1 | grepQuiet Hello
nix-instantiate --eval -E 'builtins.traceVerbose "Hello" 123' 2>&1 | grepQuietInverse Hello
nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1 | grepQuietInverse Hello
expectStderr 1 nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" (throw "Foo")' | grepQuiet Hello

nix-instantiate --eval -E 'let x = builtins.trace { x = x; } true; in x' \
  2>&1 | grepQuiet -E 'trace: { x = «potential infinite recursion»; }'

nix-instantiate --eval -E 'let x = { repeating = x; tracing = builtins.trace x true; }; in x.tracing'\
  2>&1 | grepQuiet -F 'trace: { repeating = «repeated»; tracing = «potential infinite recursion»; }'

set +x

fail=0

for i in lang/parse-fail-*.nix; do
    echo "parsing $i (should fail)";
    i=$(basename $i .nix)
    if ! expect 1 nix-instantiate --parse - < lang/$i.nix; then
        echo "FAIL: $i shouldn't parse"
        fail=1
    fi
done

for i in lang/parse-okay-*.nix; do
    echo "parsing $i (should succeed)";
    i=$(basename $i .nix)
    if ! expect 0 nix-instantiate --parse - < lang/$i.nix > lang/$i.out; then
        echo "FAIL: $i should parse"
        fail=1
    fi
done

for i in lang/eval-fail-*.nix; do
    echo "evaluating $i (should fail)";
    i=$(basename $i .nix)
    if ! expect 1 nix-instantiate --eval lang/$i.nix; then
        echo "FAIL: $i shouldn't evaluate"
        fail=1
    fi
done

for i in lang/eval-okay-*.nix; do
    echo "evaluating $i (should succeed)";
    i=$(basename $i .nix)

    if test -e lang/$i.exp; then
        flags=
        if test -e lang/$i.flags; then
            flags=$(cat lang/$i.flags)
        fi
        if ! expect 0 env NIX_PATH=lang/dir3:lang/dir4 HOME=/fake-home nix-instantiate $flags --eval --strict lang/$i.nix > lang/$i.out; then
            echo "FAIL: $i should evaluate"
            fail=1
        elif ! diff <(< lang/$i.out sed -e "s|$(pwd)|/pwd|g") lang/$i.exp; then
            echo "FAIL: evaluation result of $i not as expected"
            fail=1
        fi
    fi

    if test -e lang/$i.exp.xml; then
        if ! expect 0 nix-instantiate --eval --xml --no-location --strict \
                lang/$i.nix > lang/$i.out.xml; then
            echo "FAIL: $i should evaluate"
            fail=1
        elif ! cmp -s lang/$i.out.xml lang/$i.exp.xml; then
            echo "FAIL: XML evaluation result of $i not as expected"
            fail=1
        fi
    fi
done

exit $fail
