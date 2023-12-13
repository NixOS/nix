# This `example` tests the permissions of input dependencies of the main derivation

source "../common.sh"

USER=$(whoami)

cp ../dummy "$TEST_ROOT"
cp ../config.nix "$TEST_ROOT"
cd "$TEST_ROOT"

cat > "test.nix"<<EOF
with import ./config.nix;
mkDerivation {
  name = "example";
  # Check that importing a source works
  exampleSource = builtins.path {
    path = ./dummy;
    permissions = {
      protected = true;
      users = ["$USER"];
    };
  };
  buildCommand = "echo \$exampleSource >> \$out";
  allowSubstitutes = false;
  __permissions = {
    outputs.out = { protected = true; users = ["$USER"]; };
    drv = { protected = true; users = ["$USER"]; };
    log.protected = false;
  };
}
EOF

OUTPUT_PATH=$(nix-build "test.nix" --no-link)
EXAMPLE_SOURCE_PATH=$(cat "$OUTPUT_PATH")

nix store access info "$OUTPUT_PATH" --json | grep '"users":\["'$USER'"\]'

nix store access info "$EXAMPLE_SOURCE_PATH" --json | grep '"users":\["'$USER'"\]'
