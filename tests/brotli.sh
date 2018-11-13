source common.sh

clearStore
clearCache

if [[ "$(uname)" =~ ^MINGW|^MSYS ]]; then
    cacheURI="file://$(cygpath -m $cacheDir)?compression=br"
else
    cacheURI="file://$cacheDir?compression=br"
fi

outPath=$(nix-build dependencies.nix --no-out-link)

nix copy --to $cacheURI $outPath

HASH=$(nix hash-path $outPath)

clearStore
clearCacheCache

nix copy --from $cacheURI $outPath --no-check-sigs

HASH2=$(nix hash-path $outPath)

[[ $HASH = $HASH2 ]]
