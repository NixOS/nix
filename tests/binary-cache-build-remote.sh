source common.sh

clearStore
clearCacheCache

# Fails without remote builders
(! nix-build --store "file://$cacheDir" dependencies.nix)

# Succeeds with default store as build remote.
outPath=$(nix-build --store "file://$cacheDir" --builders 'auto - - 1 1' -j0 dependencies.nix)

# Test that the path exactly exists in the destination store.
nix path-info --store "file://$cacheDir" $outPath

# Succeeds without any build capability because no-op
nix-build --store "file://$cacheDir" -j0 dependencies.nix
