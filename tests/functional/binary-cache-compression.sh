#!/usr/bin/env bash

source common.sh

TODO_NixOS # Requires clearing the store

# This is the full list that might be supported if libarchive were to be
# compiled with the appropriate dependencies. Only a subset of those has been
# actually supported historically in nix-built nix.
declare -A compressionAlgoToExtension=(
    [none]=''
    [br]='.br'
    [bzip2]='.bz2'
    [compress]=''
    [grzip]=''
    [gzip]=''
    [lrzip]=''
    [lz4]='.lz4'
    [lzip]='.lzip'
    [lzma]=''
    [lzop]=''
    [xz]='.xz'
    [zstd]='.zst'
)

for algo in "${!compressionAlgoToExtension[@]}"; do
    clearBinaryCache

    cacheURI="file://$cacheDir?compression=$algo"
    outPath=$(nix-build dependencies.nix --no-out-link)

    case $algo in
    # Nix fails when libarchive would require shelling out to an external
    # program to support compression with a given algorithm. Don't bother
    # failing for algorithms that aren't usually linked into libarchive in
    # nixpkgs. Brotli/Zstd is supported directly.
    "none" | "br" | "bzip2" | "lzma" | "xz" | "zstd" | "gzip")
        isRequired=1
        ;;
    *)
        isRequired=0
        ;;
    esac

    set +e
    copyOut=$(nix copy --to "$cacheURI" "$outPath" 2>&1)
    copyCode=$?
    set -e

    if ((copyCode == 0)); then
        HASH=$(nix hash path "$outPath")

        clearStore
        clearCacheCache
        rm -f "$TEST_ROOT"/{profile,result}

        nix copy --from "$cacheURI" "$outPath" --no-check-sigs --profile "$TEST_ROOT/profile" --out-link "$TEST_ROOT/result"

        [[ -e $TEST_ROOT/profile ]]
        [[ -e $TEST_ROOT/result ]]

        HASH2=$(nix hash path "$outPath")

        [[ $HASH == "$HASH2" ]]

        if ls "$cacheDir/nar/"*"${compressionAlgoToExtension[$algo]}" &>/dev/null; then
            echo "files do exist"
        else
            fail "nars do not exist"
        fi
    else
        if ((isRequired == 1)); then
            fail "'$algo' compression algorithm doesn't seem to be supported"
        else
            echo "$copyOut" | grepQuiet "initialize compression"
            echo "non-mandatory compression algorithm '$algo' doesn't seem to be supported in libarchive"
        fi
    fi
done
