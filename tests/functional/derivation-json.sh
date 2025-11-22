#!/usr/bin/env bash

source common.sh

drvPath=$(nix-instantiate simple.nix)

nix derivation show "$drvPath" | jq '.[]' > "$TEST_HOME/simple.json"

# Round tripping to JSON works
drvPath2=$(nix derivation add < "$TEST_HOME/simple.json")
[[ "$drvPath" = "$drvPath2" ]]

# Derivation is input addressed, all outputs have a path
jq -e '.outputs | .[] | has("path")' < "$TEST_HOME/simple.json"

# Input addressed derivations cannot be renamed.
jq '.name = "foo"' < "$TEST_HOME/simple.json" | expectStderr 1 nix derivation add | grepQuiet "has incorrect output"

# If we remove the input addressed to make it a deferred derivation, we
# still get the same result because Nix will see that need not be
# deferred and fill in the right input address for us.
drvPath3=$(jq '.outputs |= map_values(del(.path))' < "$TEST_HOME/simple.json" | nix derivation add)
[[ "$drvPath" = "$drvPath3" ]]
