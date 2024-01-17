source ./common.sh

flake1Dir=$TEST_ROOT/flake1

mkdir -p $flake1Dir
cat > $flake1Dir/flake.nix <<EOF
{
    outputs = { self }: {
        x = 1;
        packages.$system.x = 2;
    };
}
EOF

[ "$(nix eval --impure --json $flake1Dir#.x)" -eq 1 ]
[ "$(nix eval --impure --json $flake1Dir#x)" -eq 2 ]
[ "$(nix eval --impure --json $flake1Dir#.packages.$system.x)" -eq 2 ]
