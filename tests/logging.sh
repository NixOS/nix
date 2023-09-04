source common.sh

clearStore

path=$(nix-build dependencies.nix --no-out-link)

# Test nix-store -l.
[ "$(nix-store -l $path)" = FOO ]

# Test compressed logs.
clearStore
rm -rf $NIX_LOG_DIR
(! nix-store -l $path)
nix-build dependencies.nix --no-out-link --compress-build-log
[ "$(nix-store -l $path)" = FOO ]

# test whether empty logs work fine with `nix log`.
builder="$(mktemp)"
echo -e "#!/bin/sh\nmkdir \$out" > "$builder"
outp="$(nix-build -E \
    'with import ./config.nix; mkDerivation { name = "fnord"; builder = '"$builder"'; }' \
    --out-link "$(mktemp -d)/result")"

test -d "$outp"

nix log "$outp"
