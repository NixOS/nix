source common.sh

clearStore

outPath=$($nixbuild -vv -j10000 parallel.nix)

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "abacade"; then exit 1; fi

if test "$(cat $SHARED.cur)" != 0; then exit 1; fi
if test "$(cat $SHARED.max)" != 3; then exit 1; fi
