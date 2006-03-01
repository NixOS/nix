source common.sh

drvPath=$($TOP/src/nix-instantiate/nix-instantiate parallel.nix)

echo "derivation is $drvPath"

outPath=$($TOP/src/nix-store/nix-store -qfvv -j10000 "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "abacade"; then exit 1; fi

if test "$(cat $SHARED.cur)" != 0; then exit 1; fi
if test "$(cat $SHARED.max)" != 3; then exit 1; fi
