source common.sh

clearStore
clearCacheCache

# Fails without remote builders
(! nix-build --store "file://$cacheDir" dependencies.nix)

# Succeeds with default store as build remote.
nix-build --store "file://$cacheDir" --builders 'auto - - 1 1' -j0 dependencies.nix

# Succeeds without any build capability because no-op
nix-build --store "file://$cacheDir" -j0 dependencies.nix
