source common.sh

clearStore

path=$(nix-build dependencies.nix --no-out-link)

# Test nix-store -l.
[ "$(nix-store -l $path)" = FOO ]

# Test compressed logs.
clearStore
rm -rf $NIX_LOG_DIR
(! nix-store -l $path)
nix-build dependencies.nix --no-out-link --option build-compress-log true
[ "$(nix-store -l $path)" = FOO ]
