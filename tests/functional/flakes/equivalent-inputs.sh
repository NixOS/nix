source ./common.sh
baseDir=$TEST_ROOT/$RANDOM

# Create the common flake
commonFlake=$baseDir/common
mkdir -p $commonFlake
cat > $commonFlake/flake.nix <<EOF
{
  outputs = inputs: {
    legacyPackages.$system = {
      default = builtins.trace "evaluating common flake input" 1;
    };
  };
}
EOF

# Create flake A that imports the common flake
flakeA=$baseDir/flakeA
mkdir -p $flakeA
cat > $flakeA/flake.nix <<EOF
{
  inputs.common.url = "$commonFlake";
  outputs = { common, ... }: {
    legacyPackages.$system.default = common.legacyPackages.$system.default;
  };
}
EOF

# Create flake B that also imports the common flake
flakeB=$baseDir/flakeB
cp -r $flakeA $flakeB

# Create the root flake that imports flake A, flake B
# and sums their outputs
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

(cd $commonFlake && nix flake lock)
(cd $flakeA && nix flake lock)
(cd $flakeRoot && nix flake lock)

# Evaluate the root flake. The common flake input should only be evaluated once
(cd $flakeRoot && nix eval .#default) 1> stdout 2> stderr
[[ $(cat stdout) = 2 ]]
[[ $(cat stderr | grep "trace: evaluating common flake input" | wc -l) = 1 ]]

# Create the override flake
overrideFlake=$baseDir/override
mkdir -p $overrideFlake

cat > $overrideFlake/flake.nix <<EOF
{
  outputs = inputs: {
    legacyPackages.$system = {
      default = builtins.trace "evaluating override flake input" 2;
    };
  };
}
EOF

(cd $overrideFlake && nix flake lock)

# Evaluate the root flake with overrides. Both the common and
# overriden flakes should be evaluated
(cd $flakeRoot && nix eval --override-input flakeB $overrideFlake .#default) 1> stdout 2> stderr
[[ $(cat stdout) = 3 ]]
[[ $(cat stderr | grep "trace: evaluating common flake input" | wc -l) = 1 ]]
[[ $(cat stderr | grep "trace: evaluating override flake input" | wc -l) = 1 ]]

# Same when overriding other inputs' inputs
(cd $flakeRoot && nix eval --override-input flakeB/common $overrideFlake .#default) 1> stdout 2> stderr
[[ $(cat stdout) = 3 ]]
[[ $(cat stderr | grep "trace: evaluating common flake input" | wc -l) = 1 ]]
[[ $(cat stderr | grep "trace: evaluating override flake input" | wc -l) = 1 ]]
