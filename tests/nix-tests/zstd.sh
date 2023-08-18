source common.sh

clearStore
clearCache

cacheURI="file://$cacheDir?compression=zstd"

outPath=$(nix-build dependencies.nix --no-out-link)

nix copy --to $cacheURI $outPath

HASH=$(nix hash path $outPath)

clearStore
clearCacheCache

nix copy --from $cacheURI $outPath --no-check-sigs

if ls $cacheDir/nar/*.zst &> /dev/null; then
    echo "files do exist"
else
    echo "nars do not exist"
    exit 1
fi

HASH2=$(nix hash path $outPath)

[[ $HASH = $HASH2 ]]
