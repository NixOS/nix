# This test tries to construct a runtime dependency which is missing permissions, which is not allowed and should fail.

source "../common.sh"

USER=$(whoami)

cp ../dummy "$TEST_ROOT"
cp ../config.nix "$TEST_ROOT"
cd "$TEST_ROOT"

cat > "test.nix"<<EOF
with import ./config.nix;
mkDerivation {
  name = "example";
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

# Revoking the access to a runtime dependency should fail
# [TODO] uncomment once this is implemented
# ! nix store access revoke "$EXAMPLE_SOURCE_PATH" --user "$USER"
