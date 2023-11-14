source common.sh

# Adds the "dummy" file to the nix store and check that we can access it
EXAMPLE_PATH=$(nix store add-path dummy)
nix store access info "$EXAMPLE_PATH" --json | grep '"protected":false'
cat "$EXAMPLE_PATH"
getfacl "$EXAMPLE_PATH"

# Protect a file and check that we cannot access it anymore
nix store access protect "$EXAMPLE_PATH"
! cat "$EXAMPLE_PATH"
nix store access info "$EXAMPLE_PATH" --json | grep '"protected":true'
nix store access info "$EXAMPLE_PATH" --json | grep '"users":\[\]'

USER=$(whoami)

# Grant permission and check that we can access the file
nix store access grant "$EXAMPLE_PATH" --user "$USER"
cat "$EXAMPLE_PATH"
nix store access info "$EXAMPLE_PATH" --json | grep '"users":\["'$USER'"\]'

# Revoke permission and check that we cannot access the file anymore
nix store access revoke "$EXAMPLE_PATH" --user "$USER"
nix store access info "$EXAMPLE_PATH" --json | grep '"users":\[\]'

# Check setting permissions from a nix file
cp dummy "$TEST_ROOT"
cp config.nix "$TEST_ROOT"
cat > "$TEST_ROOT/test-acls.nix"<<EOF
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
  buildCommand = "echo Example > \$out; cat \$exampleSource >> \$out";
  allowSubstitutes = false;
  __permissions = {
    outputs.out = { protected = true; users = ["$USER"]; };
    drv = { protected = true; users = ["$USER"]; };
    log.protected = false;
  };
}
EOF

OUTPUT_PATH=$(nix-build "$TEST_ROOT/test-acls.nix")
cat "$OUTPUT_PATH"
nix store access info "$OUTPUT_PATH" --json | grep '"users":\["'$USER'"\]'

# Revoke permission and check that we cannot access the file anymore
nix store access revoke "$OUTPUT_PATH" --user "$USER"
nix store access info "$OUTPUT_PATH" --json | grep '"users":\[\]'
