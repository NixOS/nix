source common.sh

requireDaemonNewerThan "2.8pre20220311"

enableFeatures "ca-derivations ca-references impure-derivations"

clearStore

# Basic test of impure derivations: building one a second time should not use the previous result.
printf 0 > $TEST_ROOT/counter

json=$(nix build -L --no-link --json --file ./impure-derivations.nix impure)
path1=$(echo $json | jq -r .[].outputs.out)
path1_stuff=$(echo $json | jq -r .[].outputs.stuff)
[[ $(< $path1/n) = 0 ]]
[[ $(< $path1_stuff/bla) = 0 ]]

[[ $(nix path-info --json $path1 | jq .[].ca) =~ fixed:r:sha256: ]]

path2=$(nix build -L --no-link --json --file ./impure-derivations.nix impure | jq -r .[].outputs.out)
[[ $(< $path2/n) = 1 ]]

# Test impure derivations that depend on impure derivations.
path3=$(nix build -L --no-link --json --file ./impure-derivations.nix impureOnImpure -vvvvv | jq -r .[].outputs.out)
[[ $(< $path3/n) = X2 ]]

path4=$(nix build -L --no-link --json --file ./impure-derivations.nix impureOnImpure -vvvvv | jq -r .[].outputs.out)
[[ $(< $path4/n) = X3 ]]

# Test that (self-)references work.
[[ $(< $path4/symlink/bla) = 3 ]]
[[ $(< $path4/self/n) = X3 ]]

# Input-addressed derivations cannot depend on impure derivations directly.
nix build -L --no-link --json --file ./impure-derivations.nix inputAddressed 2>&1 | grep 'depends on impure derivation'

drvPath=$(nix eval --json --file ./impure-derivations.nix impure.drvPath | jq -r .)
[[ $(nix show-derivation $drvPath | jq ".[\"$drvPath\"].outputs.out.impure") = true ]]
[[ $(nix show-derivation $drvPath | jq ".[\"$drvPath\"].outputs.stuff.impure") = true ]]
