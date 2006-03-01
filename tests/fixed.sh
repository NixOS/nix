source common.sh

drvs=$($nixinstantiate fixed.nix -A good)
echo $drvs
$nixstore -r $drvs

drvs=$($nixinstantiate fixed.nix -A good2)
echo $drvs
$nixstore -r $drvs

drvs=$($nixinstantiate fixed.nix -A bad)
echo $drvs
if $nixstore -r $drvs; then false; fi

if $nixinstantiate fixed.nix -A reallyBad; then false; fi

# While we're at it, check attribute selection a bit more.
test $($nixinstantiate fixed.nix -A good.1 | wc -l) = 1
