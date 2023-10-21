source common.sh

clearStore
clearCache

remoteRoot=$TEST_ROOT/store2
chmod -R u+w "$remoteRoot" || true
rm -rf "$remoteRoot"

outPath=$(nix-build --no-out-link dependencies.nix)

nix store info --store "ssh-ng://localhost?store=$NIX_STORE_DIR&remote-store=$remoteRoot%3fstore=$NIX_STORE_DIR%26real=$remoteRoot$NIX_STORE_DIR"

# Regression test for https://github.com/NixOS/nix/issues/6253
nix copy --to "ssh-ng://localhost?store=$NIX_STORE_DIR&remote-store=$remoteRoot%3fstore=$NIX_STORE_DIR%26real=$remoteRoot$NIX_STORE_DIR" $outPath --no-check-sigs &
nix copy --to "ssh-ng://localhost?store=$NIX_STORE_DIR&remote-store=$remoteRoot%3fstore=$NIX_STORE_DIR%26real=$remoteRoot$NIX_STORE_DIR" $outPath --no-check-sigs

[ -f $remoteRoot$outPath/foobar ]
