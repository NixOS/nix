#!/usr/bin/env bash

source common.sh

TODO_NixOS

# Using `--eval-store` with the daemon will eventually copy everything
# to the build store, invalidating most of the tests here
# shellcheck disable=SC1111
needLocalStore "“--eval-store” doesn't achieve much with the daemon"

eval_store=$TEST_ROOT/eval-store

nix build -f dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    # Resolved CA derivations are written to store for building
    #
    # TODO when we something more systematic
    # (https://github.com/NixOS/nix/issues/5025) that distinguishes
    # between scratch storage for building and the final destination
    # store, we'll be able to make this unconditional again -- resolved
    # derivations should only appear in the scratch store.
    (! ls "$NIX_STORE_DIR"/*.drv)
fi
ls "$eval_store"/nix/store/*.drv

clearStore
rm -rf "$eval_store"

nix-instantiate dependencies.nix --eval-store "$eval_store"
(! ls "$NIX_STORE_DIR"/*.drv)
ls "$eval_store"/nix/store/*.drv

clearStore
rm -rf "$eval_store"

nix-build dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    # See above
    (! ls "$NIX_STORE_DIR"/*.drv)
fi
ls "$eval_store"/nix/store/*.drv

clearStore
rm -rf "$eval_store"

# Confirm that import-from-derivation builds on the build store
[[ $(nix eval --eval-store "$eval_store?require-sigs=false" --impure --raw --file ./ifd.nix) = hi ]]
ls "$NIX_STORE_DIR"/*dependencies-top/foobar
(! ls "$eval_store"/nix/store/*dependencies-top/foobar)

clearStore
rm -rf "$eval_store"

# `builtins.storePath`, `builtins.appendContext`, and `builtins.fetchurl` and `builtins.fetchTarball` with a hash copy a path that only the build store has from there
echo -n "eval-store fetchurl" > "$TEST_ROOT/fetchurl-file"
path=$(nix-store --add-fixed sha256 "$TEST_ROOT/fetchurl-file")
[[ $(nix eval --eval-store "$TEST_ROOT/es1" --impure --raw --expr "builtins.storePath \"$path\"") = "$path" ]]
[[ $(nix eval --eval-store "$TEST_ROOT/es2" --impure --raw --expr "builtins.appendContext \"$path\" { \"$path\".path = true; }") = "$path" ]]
[[ $(nix eval --eval-store "$TEST_ROOT/es3" --impure --raw --expr "builtins.fetchurl { url = \"file://$TEST_ROOT/missing/fetchurl-file\"; sha256 = \"$(nix-hash --type sha256 --flat "$TEST_ROOT/fetchurl-file")\"; }") = "$path" ]]
mkdir -p "$TEST_ROOT/fetchTarball/source" && echo -n "eval-store fetchTarball" > "$TEST_ROOT/fetchTarball/source/file"
tarballPath=$(nix-store --add-fixed --recursive sha256 "$TEST_ROOT/fetchTarball/source")
[[ $(nix eval --eval-store "$TEST_ROOT/es7" --impure --raw --expr "builtins.fetchTarball { url = \"file://$TEST_ROOT/missing/source.tar\"; sha256 = \"$(nix-hash --type sha256 "$TEST_ROOT/fetchTarball/source")\"; }") = "$tarballPath" ]]
# A path that neither store has fails as before, without trying to copy it
missingPath="$NIX_STORE_DIR/00000000000000000000000000000000-eval-store-missing"
missingErr=$(expectStderr 1 nix eval --eval-store "$TEST_ROOT/es1" --impure --expr "builtins.storePath \"$missingPath\"")
grepQuiet "path '$missingPath' is required, but there is no substituter" <<< "$missingErr"
grepQuietInverse "is not valid" <<< "$missingErr"

# The closure of an input-addressed path is copied whole. A local eval store
# refuses a path whose references it lacks, so copying the path alone already
# fails the `nix eval` below; the `path-info --recursive` diff only checks the result.
outPath=$(nix-build dependencies.nix --no-out-link)
storePathExpr="builtins.storePath \"$outPath\""
[[ $(nix eval --eval-store "$TEST_ROOT/es4?require-sigs=false" --impure --raw --expr "$storePathExpr") = "$outPath" ]]
diff <(nix path-info --store "$TEST_ROOT/es4" --recursive "$outPath") <(nix path-info --recursive "$outPath")
# A substituter is still tried first: the eval store gets the path from a substituter that has it signed, not the build store's unsigned copy
nix-store --generate-binary-cache-key eval-store-test "$TEST_ROOT/eval-store-sk" "$TEST_ROOT/eval-store-pk"
nix copy --to "file://$TEST_ROOT/eval-store-cache?secret-key=$TEST_ROOT/eval-store-sk" "$outPath"
# (`nix` turns substitution off by default when it finds no Internet access)
[[ $(nix eval --eval-store "$TEST_ROOT/es5?require-sigs=false" --option substitute true --substituters "file://$TEST_ROOT/eval-store-cache" \
    --trusted-public-keys "$(cat "$TEST_ROOT/eval-store-pk")" --impure --raw --expr "$storePathExpr") = "$outPath" ]]
diff <(nix path-info --store "$TEST_ROOT/es5" --recursive "$outPath") <(nix path-info --recursive "$outPath")
# Accepting either copy, it took the substituter's: that one carries the signature
nix path-info --store "$TEST_ROOT/es5" --sigs "$outPath" | grepQuiet "eval-store-test:"
# Without that substituter, the unsigned copy is refused (content-addressed paths need no signature).
# That is the error shown, also below several stack frames and without `--show-trace` (which the test configuration sets)
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    expectStderr 1 nix eval --eval-store "$TEST_ROOT/es6" --option show-trace false --impure --raw \
        --expr "let f = n: if n == 0 then $storePathExpr else f (n - 1) + \"\"; in f 5" | grepQuiet "lacks a signature by a trusted key"
fi

clearStore
chmod -R u+w "$TEST_ROOT/es4" "$TEST_ROOT/es5" "$TEST_ROOT/es7"
rm -rf "$TEST_ROOT/es1" "$TEST_ROOT/es2" "$TEST_ROOT/es3" "$TEST_ROOT/es4" "$TEST_ROOT/es5" "$TEST_ROOT/es6" "$TEST_ROOT/es7"

[[ $(nix develop -f ./shell.nix shellDrv --eval-store "$eval_store" -c printenv VAR_FROM_NIX) = bar ]]

# Can't write .drv by default
(! nix-instantiate dependencies.nix --eval-store "dummy://")
nix-instantiate dependencies.nix --eval-store "dummy://?read-only=false"
