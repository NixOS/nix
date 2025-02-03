#!/usr/bin/env bash
# docker.nix test script. Runs inside a built docker.nix container.

set -eEuo pipefail

export NIX_CONFIG='substituters = http://cache:5000?trusted=1'

cd /tmp

# Test getting a fetched derivation
test "$("$(nix-build -E '(import <nixpkgs> {}).hello')"/bin/hello)" = "Hello, world!"

# Test building a simple derivation
# shellcheck disable=SC2016
nix-build -E '
let
  pkgs = import <nixpkgs> {};
in
builtins.derivation {
  name = "test";
  system = builtins.currentSystem;
  builder = "${pkgs.bash}/bin/bash";
  args = ["-c" "echo OK > $out"];
}'
test "$(cat result)" = OK

# Ensure #!/bin/sh shebang works
echo '#!/bin/sh' > ./shebang-test
echo 'echo OK' >> ./shebang-test
chmod +x ./shebang-test
test "$(./shebang-test)" = OK

# Ensure #!/usr/bin/env shebang works
echo '#!/usr/bin/env bash' > ./shebang-test
echo 'echo OK' >> ./shebang-test
chmod +x ./shebang-test
test "$(./shebang-test)" = OK

# Test nix-shell
{
    echo '#!/usr/bin/env nix-shell'
    echo '#! nix-shell -i bash'
    echo '#! nix-shell -p hello'
    echo 'hello'
} > ./nix-shell-test
chmod +x ./nix-shell-test
test "$(./nix-shell-test)" = "Hello, world!"
