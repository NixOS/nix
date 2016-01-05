source common.sh

export NIX_BUILD_HOOK="$(pwd)/build-hook.hook.sh"

outPath=$(nix-build build-hook.nix --no-out-link)

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "BARBAR"; then exit 1; fi
