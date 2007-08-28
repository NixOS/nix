source common.sh

clearStore

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
