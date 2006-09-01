source common.sh

case $system in
    *linux*)
        ;;
    *)
        exit 0;
esac

set -m # enable job control, needed for kill

profiles="$NIX_STATE_DIR"/profiles
rm -f $profiles/*

$nixenv -p $profiles/test -f ./gc-runtime.nix -i gc-runtime

outPath=$($nixenv -p $profiles/test -q --no-name --out-path gc-runtime)
echo $outPath

echo "backgrounding program..."
$profiles/test/program &
sleep 2 # hack - wait for the program to get started
child=$!
echo PID=$child

$nixenv -p $profiles/test -e gc-runtime
$nixenv -p $profiles/test --delete-generations old

cp $TOP/scripts/find-runtime-roots.pl $TEST_ROOT/foo.pl
chmod +x $TEST_ROOT/foo.pl
NIX_ROOT_FINDER=$TEST_ROOT/foo.pl $nixstore --gc

kill -- -$child

if ! test -e $outPath; then
    echo "running program was garbage collected!"
    exit 1
fi

exit 0
