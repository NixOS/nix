#!/usr/bin/env bash

source common.sh

set -o pipefail

source characterisation/framework.sh

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
# Relies on parsing the expression derivation as a derivation, can't use --eval
expectStderr 1 nix-instantiate --show-trace lang/non-eval-fail-bad-drvPath.nix | grepQuiet "store path '8qlfcic10lw5304gqm8q45nr7g7jl62b-cachix-1.7.3-bin' is not a valid derivation path"


nix-instantiate --eval -E 'let x = builtins.trace { x = x; } true; in x' \
  2>&1 | grepQuiet -E 'trace: { x = «potential infinite recursion»; }'

nix-instantiate --eval -E 'let x = { repeating = x; tracing = builtins.trace x true; }; in x.tracing'\
  2>&1 | grepQuiet -F 'trace: { repeating = «repeated»; tracing = «potential infinite recursion»; }'

nix-instantiate --eval -E 'builtins.warn "Hello" 123' 2>&1 | grepQuiet 'warning: Hello'
# shellcheck disable=SC2016 # The ${} in this is Nix, not shell
nix-instantiate --eval -E 'builtins.addErrorContext "while doing ${"something"} interesting" (builtins.warn "Hello" 123)' 2>/dev/null | grepQuiet 123

# warn does not accept non-strings for now
expectStderr 1 nix-instantiate --eval -E 'let x = builtins.warn { x = x; } true; in x' \
  | grepQuiet "expected a string but found a set"
expectStderr 1 nix-instantiate --eval --abort-on-warn -E 'builtins.warn "Hello" 123' | grepQuiet Hello
# shellcheck disable=SC2016 # The ${} in this is Nix, not shell
NIX_ABORT_ON_WARN=1 expectStderr 1 nix-instantiate --eval -E 'builtins.addErrorContext "while doing ${"something"} interesting" (builtins.warn "Hello" 123)' | grepQuiet "while doing something interesting"

set +x

badDiff=0
badExitCode=0

# Extra post-processing that's specific to each test case
postprocess() {
    if [[ -e "lang/$1.postprocess" ]]; then
        (
            # We could allow arbitrary interpreters in .postprocess, but that
            # just exposes us to the complexity of not having /usr/bin/env in
            # the sandbox. So let's just hardcode bash for now.
            set -x;
            bash "lang/$1.postprocess" "lang/$1"
        )
    fi
}

for i in lang/parse-fail-*.nix; do
    echo "parsing $i (should fail)";
    i=$(basename "$i" .nix)
    if expectStderr 1 nix-instantiate --parse - < "lang/$i.nix" > "lang/$i.err"
    then
        postprocess "$i"
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
        postprocess "$i"
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
            # note that show-trace is also set by common/init.sh
            echo "--eval --strict --show-trace"
        fi
    )"
    if
        # shellcheck disable=SC2086 # word splitting of flags is intended
        expectStderr 1 nix-instantiate $flags "lang/$i.nix" \
            | sed "s!$(pwd)!/pwd!g" > "lang/$i.err"
    then
        postprocess "$i"
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
            postprocess "$i"
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
            postprocess "$i"
            diffAndAccept "$i" out exp
            diffAndAccept "$i" err err.exp
        else
            echo "FAIL: $i should evaluate"
            badExitCode=1
        fi
    fi
done

characterisationTestExit
