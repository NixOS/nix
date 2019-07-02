# Test the `--timeout' option.

source common.sh


set +e
messages=$(nix-build -Q timeout.nix -A infiniteLoop --timeout 2 2>&1)
status=$?
set -e

if [ $status -ne 101 ]; then
    echo "error: 'nix-store' exited with '$status'; should have exited 101"
    exit 1
fi

if ! echo "$messages" | grep -q "timed out"; then
    echo "error: build may have failed for reasons other than timeout; output:"
    echo "$messages" >&2
    exit 1
fi

if nix-build -Q timeout.nix -A infiniteLoop --max-build-log-size 100; then
    echo "build should have failed"
    exit 1
fi

if nix-build timeout.nix -A silent --max-silent-time 2; then
    echo "build should have failed"
    exit 1
fi

if nix-build timeout.nix -A closeLog; then
    echo "build should have failed"
    exit 1
fi

if nix build -f timeout.nix silent --max-silent-time 2; then
    echo "build should have failed"
    exit 1
fi
