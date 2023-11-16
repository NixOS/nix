# Run using: make tests/acls/protect_dep.sh.test

# This `example` derivation takes $exampleSource as input.
# Both `example` and `exampleSource` permissions are set to $USER in the nix file,
# But using `nix store access info` it is only the case for `example`.

# Note: The output of `example` contains the path of `$exampleSource` to be able to recover it in the test.
# This makes `exampleSource` part of the runtime closure of `example` but even without this, shouldn't the permissions be the same ?

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

# This is successful
nix store access info "$OUTPUT_PATH" --json | grep '"users":\["'$USER'"\]'

# However we have the following outputs:
# {"exists":true,"groups":[],"protected":true,"users":[]}
nix store access info "$EXAMPLE_SOURCE_PATH" --json

# So this fails:
nix store access info "$EXAMPLE_SOURCE_PATH" --json | grep '"users":\["'$USER'"\]'
