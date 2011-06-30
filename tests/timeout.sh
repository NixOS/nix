# Test the `--timeout' option.

source common.sh

drvPath=$($nixinstantiate timeout.nix)

test "$($nixstore -q --binding system "$drvPath")" = "$system"

echo "derivation is $drvPath"

failed=0
messages="`$nixstore -r --timeout 2 $drvPath 2>&1 || failed=1`"
if test $failed -ne 0; then
    echo "error: \`nix-store' succeeded; should have timed out" >&2
    exit 1
fi

if ! echo "$messages" | grep "timed out"; then
    echo "error: \`nix-store' may have failed for reasons other than timeout" >&2
    echo >&2
    echo "output of \`nix-store' follows:" >&2
    echo "$messages" >&2
    exit 1
fi
