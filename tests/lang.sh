source common.sh

export TEST_VAR=foo # for eval-okay-getenv.nix
export NIX_REMOTE=dummy://

nix-instantiate --eval -E 'builtins.trace "Hello" 123' 2>&1 | grep -q Hello
(! nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1 | grep -q Hello)
nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" (throw "Foo")' 2>&1 | grep -q Hello

set +x

fail=0
just_failed=0

safe_expect () {
    expected=$1 name=$2 msg=$3 cmd=$4
    just_failed=0
    res=0
    eval "$cmd" || res=$?

    if [ $res -ge 2 ]; then
        [ $res -le 128 ] || { echo "FAIL: $name returned unexpected code '$res' (should be 0 or 1)"; just_failed=1; }
        [ $res -gt 128 ] || { echo "FAIL: $name crashed with $res ($(kill -l $(($res - 128))))"; just_failed=1; }
    elif [ $expected = "fail" ]; then
        [ $res -ne 0 ] || { echo "FAIL: $name $msg"; just_failed=1; }
    elif [ $expected = "pass" ]; then
        [ $res -eq 0 ] || { echo "FAIL: $name $msg"; just_failed=1; }
    else
        echo "usage: safe_expect [pass|fail] name msg command"
        exit 1
    fi

    [ $just_failed -eq 0 ] || fail=1
}


for i in lang/parse-fail-*.nix; do
    echo "parsing $i (should fail)";
    i=$(basename $i .nix)
    safe_expect fail "$i" "shouldn't parse" \
        "nix-instantiate --parse - < lang/$i.nix"
done

for i in lang/parse-okay-*.nix; do
    echo "parsing $i (should succeed)";
    i=$(basename $i .nix)
    safe_expect pass "$i" "should parse" \
        "nix-instantiate --parse - < lang/$i.nix > lang/$i.out"
done

for i in lang/eval-fail-*.nix; do
    echo "evaluating $i (should fail)";
    i=$(basename $i .nix)
    safe_expect fail "$i" "shouldn't evaluate" \
        "nix-instantiate --eval lang/$i.nix"
done

for i in lang/eval-okay-*.nix; do
    echo "evaluating $i (should succeed)";
    i=$(basename $i .nix)

    if test -e lang/$i.exp; then
        flags=
        if test -e lang/$i.flags; then
            flags='$'"(cat lang/$i.flags)" # because vi does not highlight "\$(...)" properly ;-).
        fi
        safe_expect pass "$i" "should evaluate" \
            "NIX_PATH=lang/dir3:lang/dir4 nix-instantiate $flags --eval --strict lang/$i.nix > lang/$i.out"
        if [ $just_failed -eq 0 ] && ! diff lang/$i.out lang/$i.exp; then
            echo "FAIL: evaluation result of $i not as expected"
            fail=1
        fi
    fi

    if test -e lang/$i.exp.xml; then
        safe_expect pass "$i" "should evaluate" \
            "nix-instantiate --eval --xml --no-location --strict lang/$i.nix > lang/$i.out.xml"
        if [ $just_failed -eq 0 ] && ! cmp -s lang/$i.out.xml lang/$i.exp.xml; then
            echo "FAIL: XML evaluation result of $i not as expected"
            fail=1
        fi
    fi
done

exit $fail
