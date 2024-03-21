# source ./common.sh

test_equivalent_inputs() {
    baseDir=$TEST_ROOT/$RANDOM
    sharedFlake=$baseDir/shared
    mkdir -p $sharedFlake

    cat > $sharedFlake/flake.nix <<EOF
{
  outputs = inputs: {
    legacyPackages.$system = {
      default = builtins.trace "evaluating shared flake input" 1;
    };
  };
}
EOF
    (cd $sharedFlake && nix flake lock)

    flakeA=$baseDir/flakeA
    mkdir -p $flakeA
    cat > $flakeA/flake.nix <<EOF
{
  inputs.shared.url = "$sharedFlake";
  outputs = { shared, ... }: {
    legacyPackages.$system.default = shared.legacyPackages.$system.default;
  };
}
EOF
    (cd $flakeA && nix flake lock)

    flakeB=$baseDir/flakeB
    cp -r $flakeA $flakeB

    flakeRoot=$baseDir/flakeRoot
    mkdir -p $flakeRoot
    cat > $flakeRoot/flake.nix <<EOF
{
  inputs.flakeA.url = "$flakeA";
  inputs.flakeB.url = "$flakeB";
  outputs = { flakeA, flakeB, ... }: {
    legacyPackages.$system.default =
          flakeA.legacyPackages.$system.default
        + flakeB.legacyPackages.$system.default;
  };
}
EOF
    (cd $flakeRoot && nix flake lock)
    (cd $flakeRoot && nix eval .#default) 1> stdout 2> stderr
    [[ $(cat stdout) = 2 ]]
    [[ $(cat stderr | grep "trace: evaluating shared flake input" | wc -l) = 1 ]]
}
test_equivalent_inputs

# TODO(szlend): Test overrides
