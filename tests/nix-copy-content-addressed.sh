source common.sh

clearStore
clearCache

commonArgs=( \
    --experimental-features 'nix-command flakes ca-derivations ca-references' \
    --file ./content-addressed.nix \
    -v
)


nix build "${commonArgs[@]}" --no-link
nix copy --to file://$cacheDir "${commonArgs[@]}"
clearStore
drvOutput="$(nix eval --raw "${commonArgs[@]}" transitivelyDependentCA.drvPath)!out"
nix copy --from file://$cacheDir "$drvOutput" --no-require-sigs --experimental-features 'ca-references nix-command -ca-derivations'
# nix build "${commonArgs[@]}" --no-link |& (! grep -q building)
