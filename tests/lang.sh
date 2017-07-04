export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

export TEST_VAR=foo # for eval-okay-getenv.nix

   nix-instantiate              --eval -E 'builtins.trace "Hello" 123' 2>&1 | grep -q Hello
(! nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1 | grep -q Hello)
   nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" (throw "Foo")' 2>&1 | grep -q Hello

fail=0
mkdir -p lang # for output

for i in $NIX_TEST_ROOT/lang/parse-fail-*.nix; do
    echo "parsing $i (should fail)";
    i=$(basename $i .nix)
    if nix-instantiate --parse - < $NIX_TEST_ROOT/lang/$i.nix; then
        echo "FAIL: $i shouldn't parse"
        fail=1
    fi
done

for i in $NIX_TEST_ROOT/lang/parse-okay-*.nix; do
    echo "parsing $i (should succeed)";
    i=$(basename $i .nix)
    if ! nix-instantiate --parse - < $NIX_TEST_ROOT/lang/$i.nix > lang/$i.out; then
        echo "FAIL: $i should parse"
        fail=1
    fi
done

for i in $NIX_TEST_ROOT/lang/eval-fail-*.nix; do
    echo "evaluating $i (should fail)";
    i=$(basename $i .nix)
    if nix-instantiate --eval $NIX_TEST_ROOT/lang/$i.nix; then
        echo "FAIL: $i shouldn't evaluate"
        fail=1
    fi
done

for i in $NIX_TEST_ROOT/lang/eval-okay-*.nix; do
    echo "evaluating $i (should succeed)";
    i=$(basename $i .nix)

    if test -e $NIX_TEST_ROOT/lang/$i.exp; then
        flags=
        if test -e lang/$i.flags; then
            flags=$(cat lang/$i.flags)
        fi
        if ! NIX_PATH=$NIX_TEST_ROOT/lang/dir3:$NIX_TEST_ROOT/lang/dir4 nix-instantiate $flags --eval --strict $NIX_TEST_ROOT/lang/$i.nix > lang/$i.out; then
            echo "FAIL: $i should evaluate"
            fail=1
        elif ! diff lang/$i.out $NIX_TEST_ROOT/lang/$i.exp; then
            echo "FAIL: evaluation result of $i not as expected"
            fail=1
        fi
    fi

    if test -e $NIX_TEST_ROOT/lang/$i.exp.xml; then
        if ! nix-instantiate --eval --xml --no-location --strict \
                $NIX_TEST_ROOT/lang/$i.nix > lang/$i.out.xml; then
            echo "FAIL: $i should evaluate"
            fail=1
        elif ! cmp -s lang/$i.out.xml $NIX_TEST_ROOT/lang/$i.exp.xml; then
            echo "FAIL: XML evaluation result of $i not as expected"
            fail=1
        fi
    fi
done

exit $fail
