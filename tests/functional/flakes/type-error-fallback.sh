#!/usr/bin/env bash

source ./common.sh

# Test that attribute path resolution correctly handles TypeError fallback
# when packages.*.foo is a string but legacyPackages.*.foo is an attrset

flakeDir=$TEST_ROOT/type-error-fallback

mkdir -p "$flakeDir"
cat > "$flakeDir"/flake.nix <<EOF
{
    outputs = { self }: {
        # packages.*.hello is a string (not a path, just a string value)
        packages.$system.hello = "packages-hello";

        # legacyPackages.*.hello is an attrset with .out
        legacyPackages.$system.hello = {
            type = "derivation";
            out = "legacyPackages-hello-out";
            outputName = "out";
        };
    };
}
EOF

# Test 1: #hello should pick packages (the string)
result=$(nix eval --impure --raw "$flakeDir#hello")
[[ "$result" == "packages-hello" ]]

# Test 2: #hello.out should fallback to legacyPackages after TypeError
# (because packages.hello is a string, not an attrset with .out)
result=$(nix eval --impure --raw "$flakeDir#hello.out")
[[ "$result" == "legacyPackages-hello-out" ]]

# Test 3: Explicitly accessing packages.*.hello.out should fail
# (when directly specified, no fallback happens)
expect 1 nix eval --impure "$flakeDir#packages.$system.hello.out" 2>&1 | \
    grepQuiet "does not provide attribute"

# Test 4: Explicitly accessing legacyPackages.*.hello.out should succeed
result=$(nix eval --impure --raw "$flakeDir#legacyPackages.$system.hello.out")
[[ "$result" == "legacyPackages-hello-out" ]]
