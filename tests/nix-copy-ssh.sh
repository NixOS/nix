source common.sh

clearStore
clearCache

remoteRoot=$TEST_ROOT/store2
chmod -R u+w "$remoteRoot" || true
rm -rf "$remoteRoot"

outPath=$(nix-build --no-out-link dependencies.nix)

nix copy --to "ssh://localhost?store=$NIX_STORE_DIR&remote-store=$remoteRoot%3fstore=$NIX_STORE_DIR%26real=$remoteRoot$NIX_STORE_DIR" $outPath

[ -f $remoteRoot$outPath/foobar ]

clearStore

nix copy --no-check-sigs --from "ssh://localhost?store=$NIX_STORE_DIR&remote-store=$remoteRoot%3fstore=$NIX_STORE_DIR%26real=$remoteRoot$NIX_STORE_DIR" $outPath

[ -f $outPath/foobar ]
