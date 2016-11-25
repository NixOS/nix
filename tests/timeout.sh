# Test the `--timeout' option.

source common.sh

failed=0
messages="`nix-build -Q timeout.nix --timeout 2 2>&1 || failed=1`"
if [ $failed -ne 0 ]; then
    echo "error: ‘nix-store’ succeeded; should have timed out"
    exit 1
fi

if ! echo "$messages" | grep -q "timed out"; then
    echo "error: build may have failed for reasons other than timeout; output:"
    echo "$messages" >&2
    exit 1
fi

if nix-build -Q timeout.nix --option build-max-log-size 100; then
    echo "build should have failed"
    exit 1
fi
