source common.sh

set -o pipefail

source lang/framework.sh

# specialize function a bit
function diffAndAccept() {
    local -r testName="$1"
    local -r got="lang/$testName.$2"
    local -r expected="lang/$testName.$3"
    diffAndAcceptInner "$testName" "$got" "$expected"
}

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
expectStderr 1 nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello %" (throw "Foo")' | grepQuiet 'Hello %'

nix-instantiate --eval -E 'let x = builtins.trace { x = x; } true; in x' \
  2>&1 | grepQuiet -E 'trace: { x = «potential infinite recursion»; }'

nix-instantiate --eval -E 'let x = { repeating = x; tracing = builtins.trace x true; }; in x.tracing'\
  2>&1 | grepQuiet -F 'trace: { repeating = «repeated»; tracing = «potential infinite recursion»; }'

set +x

badDiff=0
badExitCode=0

for i in lang/parse-fail-*.nix; do
    echo "parsing $i (should fail)";
    i=$(basename "$i" .nix)
    if expectStderr 1 nix-instantiate --parse - < "lang/$i.nix" > "lang/$i.err"
    then
        diffAndAccept "$i" err err.exp
    else
        echo "FAIL: $i shouldn't parse"
        badExitCode=1
    fi
done

for i in lang/parse-okay-*.nix; do
    echo "parsing $i (should succeed)";
    i=$(basename "$i" .nix)
    if
        expect 0 nix-instantiate --parse - < "lang/$i.nix" \
            1> "lang/$i.out" \
            2> "lang/$i.err"
    then
        sed "s!$(pwd)!/pwd!g" "lang/$i.out" "lang/$i.err"
        diffAndAccept "$i" out exp
        diffAndAccept "$i" err err.exp
    else
        echo "FAIL: $i should parse"
        badExitCode=1
    fi
done

for i in lang/eval-fail-*.nix; do
    echo "evaluating $i (should fail)";
    i=$(basename "$i" .nix)
    flags="$(
        if [[ -e "lang/$i.flags" ]]; then
            sed -e 's/#.*//' < "lang/$i.flags"
        else
            # note that show-trace is also set by init.sh
            echo "--eval --strict --show-trace"
        fi
    )"
    if
        expectStderr 1 nix-instantiate $flags "lang/$i.nix" \
            | sed "s!$(pwd)!/pwd!g" > "lang/$i.err"
    then
        diffAndAccept "$i" err err.exp
    else
        echo "FAIL: $i shouldn't evaluate"
        badExitCode=1
    fi
done

for i in lang/eval-okay-*.nix; do
    echo "evaluating $i (should succeed)";
    i=$(basename "$i" .nix)

    if test -e "lang/$i.exp.xml"; then
        if expect 0 nix-instantiate --eval --xml --no-location --strict \
                "lang/$i.nix" > "lang/$i.out.xml"
        then
            diffAndAccept "$i" out.xml exp.xml
        else
            echo "FAIL: $i should evaluate"
            badExitCode=1
        fi
    elif test ! -e "lang/$i.exp-disabled"; then
        declare -a flags=()
        if test -e "lang/$i.flags"; then
            read -r -a flags < "lang/$i.flags"
        fi

        if
            expect 0 env \
                NIX_PATH=lang/dir3:lang/dir4 \
                HOME=/fake-home \
                nix-instantiate "${flags[@]}" --eval --strict "lang/$i.nix" \
                1> "lang/$i.out" \
                2> "lang/$i.err"
        then
            sed -i "s!$(pwd)!/pwd!g" "lang/$i.out" "lang/$i.err"
            diffAndAccept "$i" out exp
            diffAndAccept "$i" err err.exp
        else
            echo "FAIL: $i should evaluate"
            badExitCode=1
        fi
    fi
done

if test -n "${_NIX_TEST_ACCEPT-}"; then
    if (( "$badDiff" )); then
        echo 'Output did mot match, but accepted output as the persisted expected output.'
        echo 'That means the next time the tests are run, they should pass.'
    else
        echo 'NOTE: Environment variable _NIX_TEST_ACCEPT is defined,'
        echo 'indicating the unexpected output should be accepted as the expected output going forward,'
        echo 'but no tests had unexpected output so there was no expected output to update.'
    fi
    if (( "$badExitCode" )); then
        exit "$badExitCode"
    else
        skipTest "regenerating golden masters"
    fi
else
    if (( "$badDiff" )); then
        echo ''
        echo 'You can rerun this test with:'
        echo ''
        echo '    _NIX_TEST_ACCEPT=1 make tests/functional/lang.sh.test'
        echo ''
        echo 'to regenerate the files containing the expected output,'
        echo 'and then view the git diff to decide whether a change is'
        echo 'good/intentional or bad/unintentional.'
        echo 'If the diff contains arbitrary or impure information,'
        echo 'please improve the normalization that the test applies to the output.'
    fi
    exit $(( "$badExitCode" + "$badDiff" ))
fi
