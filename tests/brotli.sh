source common.sh


# Only test if we found brotli libraries
# (CLI tool is likely unavailable if libraries are missing)
if [ -n "$HAVE_BROTLI" ]; then

clearStore
clearCache

cacheURI="file://$cacheDir?compression=br"

outPath=$(nix-build dependencies.nix --no-out-link)

nix copy --to $cacheURI $outPath

HASH=$(nix hash-path $outPath)

clearStore
clearCacheCache

nix copy --from $cacheURI $outPath --no-check-sigs

HASH2=$(nix hash-path $outPath)

[[ $HASH = $HASH2 ]]

fi # HAVE_BROTLI
