source common.sh

clearStore

export IMPURE_VAR1=foo
export IMPURE_VAR2=bar

echo 'testing good...'
drvs=$($nixinstantiate fixed.nix -A good)
echo $drvs
$nixstore -r $drvs

echo 'testing good2...'
drvs=$($nixinstantiate fixed.nix -A good2)
echo $drvs
$nixstore -r $drvs

echo 'testing bad...'
drvs=$($nixinstantiate fixed.nix -A bad)
echo $drvs
if $nixstore -r $drvs; then false; fi

echo 'testing reallyBad...'
if $nixinstantiate fixed.nix -A reallyBad; then false; fi

# While we're at it, check attribute selection a bit more.
echo 'testing attribute selection...'
test $($nixinstantiate fixed.nix -A good.1 | wc -l) = 1

# Test parallel builds of derivations that produce the same output.
# Only one should run at the same time.
echo 'testing parallelSame...'
clearStore
drvs=$($nixinstantiate fixed.nix -A parallelSame)
echo $drvs
$nixstore -r $drvs -j2

# Fixed-output derivations with a recursive SHA-256 hash should
# produce the same path as "nix-store --add".
echo 'testing sameAsAdd...'
drv=$($nixinstantiate fixed.nix -A sameAsAdd)
echo $drv
out=$($nixstore -r $drv)
echo $out

# This is what fixed.builder2 produces...
rm -rf $TEST_ROOT/fixed
mkdir $TEST_ROOT/fixed
mkdir $TEST_ROOT/fixed/bla
echo "Hello World!" > $TEST_ROOT/fixed/foo
ln -s foo $TEST_ROOT/fixed/bar

out2=$($nixstore --add $TEST_ROOT/fixed)
echo $out2
test "$out" = "$out2" || exit 1

out3=$($nixstore --add-fixed --recursive sha256 $TEST_ROOT/fixed)
echo $out3
test "$out" = "$out3" || exit 1

out4=$($nixstore --print-fixed-path --recursive sha256 "1ixr6yd3297ciyp9im522dfxpqbkhcw0pylkb2aab915278fqaik" fixed)
echo $out4
test "$out" = "$out4" || exit 1
