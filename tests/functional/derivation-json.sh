#!/usr/bin/env bash

source common.sh

drvPath=$(nix-instantiate simple.nix)

nix derivation show "$drvPath" | jq '.derivations[]' > "$TEST_HOME/simple.json"

# The current JSON format is version 5
[[ $(jq .version < "$TEST_HOME/simple.json") = 5 ]]

# Round tripping to JSON works
drvPath2=$(nix derivation add < "$TEST_HOME/simple.json")
[[ "$drvPath" = "$drvPath2" ]]

# The legacy JSON format (version 4) can still be emitted...
nix derivation show --json-format 4 "$drvPath" | jq '.derivations[]' > "$TEST_HOME/simple-v4.json"
[[ $(jq .version < "$TEST_HOME/simple-v4.json") = 4 ]]

# ...without first-class options (they are encoded in the environment
# variables instead), and with plain-string environment variable values...
jq -e '(has("options") | not) and ([.env[] | type == "string"] | all)' < "$TEST_HOME/simple-v4.json"

# ...and round trips through `nix derivation add` too.
drvPathV4=$(nix derivation add < "$TEST_HOME/simple-v4.json")
[[ "$drvPath" = "$drvPathV4" ]]

# Derivation is input addressed, all outputs have a path
jq -e '.outputs | .[] | has("path")' < "$TEST_HOME/simple.json"

# Input addressed derivations cannot be renamed.
jq '.name = "foo"' < "$TEST_HOME/simple.json" | expectStderr 1 nix derivation add | grepQuiet "has incorrect output"

# If we remove the input addressed to make it a deferred derivation, we
# still get the same result because Nix will see that need not be
# deferred and fill in the right input address for us.
drvPath3=$(jq '.outputs |= map_values(del(.path))' < "$TEST_HOME/simple.json" | nix derivation add)
[[ "$drvPath" = "$drvPath3" ]]
