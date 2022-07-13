source ../common.sh

registry=$TEST_ROOT/registry.json

writeSimpleFlake() {
    local flakeDir="$1"
    cat > $flakeDir/flake.nix <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    packages.$system = rec {
      foo = import ./simple.nix;
      default = foo;
    };

    # To test "nix flake init".
    legacyPackages.x86_64-linux.hello = import ./simple.nix;
  };
}
EOF

    cp ../simple.nix ../simple.builder.sh ../config.nix $flakeDir/
}

writeDependentFlake() {
    local flakeDir="$1"
    cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self, flake1 }: {
    packages.$system.default = flake1.packages.$system.default;
    expr = assert builtins.pathExists ./flake.lock; 123;
  };
}
EOF
}
