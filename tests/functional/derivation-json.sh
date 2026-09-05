#!/usr/bin/env bash

source common.sh

drvPath=$(nix-instantiate simple.nix)

nix derivation show "$drvPath" | jq '.derivations[]' > "$TEST_HOME/simple.json"

# Round tripping to JSON works
drvPath2=$(nix derivation add < "$TEST_HOME/simple.json")
[[ "$drvPath" = "$drvPath2" ]]

nix derivation show --cbor "$drvPath" > "$TEST_HOME/simple.cbor"
drvPathCbor=$(nix derivation add --cbor < "$TEST_HOME/simple.cbor")
[[ "$drvPath" = "$drvPathCbor" ]]
[[ "$drvPath" = "$(nix derivation add --cbor --dry-run < "$TEST_HOME/simple.cbor")" ]]
nix derivation show --cbor "$drvPathCbor" > "$TEST_HOME/simple-roundtrip.cbor"
cmp "$TEST_HOME/simple.cbor" "$TEST_HOME/simple-roundtrip.cbor"
nix derivation show --cbor --recursive "$drvPath" > "$TEST_HOME/recursive.cbor"
test -s "$TEST_HOME/recursive.cbor"
depDrvPath=$(nix-instantiate dependencies.nix)
nix derivation show --cbor --recursive "$depDrvPath" > "$TEST_HOME/dependencies.cbor"
nix derivation show --cbor "$drvPath" "$depDrvPath" > "$TEST_HOME/multiple.cbor"
nix derivation show --cbor "$depDrvPath" "$drvPath" > "$TEST_HOME/multiple-reversed.cbor"
cmp "$TEST_HOME/multiple.cbor" "$TEST_HOME/multiple-reversed.cbor"
expectStderr 1 nix derivation add --cbor < "$TEST_HOME/simple.json" | grepQuiet 'CBOR'

verbatimDrv=$(nix-instantiate --expr 'with import ./config.nix; mkDerivation {
  name = "verbatim-structured-attrs";
  __json = "{ \"z\": 0, \"a\": 1 }";
}')
nix derivation show --cbor "$verbatimDrv" > "$TEST_HOME/verbatim.cbor"
[[ "$verbatimDrv" = "$(nix derivation add --cbor < "$TEST_HOME/verbatim.cbor")" ]]
nix derivation show --cbor "$verbatimDrv" > "$TEST_HOME/verbatim-roundtrip.cbor"
cmp "$TEST_HOME/verbatim.cbor" "$TEST_HOME/verbatim-roundtrip.cbor"

# Invalid UTF-8 must survive in every byte-valued field, including env keys.
printf '\200\377' > "$TEST_HOME/arbitrary-bytes"
bytesDrv=$(nix-instantiate --argstr bytesFile "$TEST_HOME/arbitrary-bytes" --expr '
  { bytesFile }:
  let bytes = builtins.readFile bytesFile; in
  derivation {
    name = "arbitrary-bytes";
    system = (import ./config.nix).system;
    builder = "/builder-${bytes}";
    args = [ "" bytes ];
    "${bytes}" = bytes;
    payload = bytes;
  }
')
nix derivation show --cbor "$bytesDrv" > "$TEST_HOME/bytes.cbor"
[[ "$bytesDrv" = "$(nix derivation add --cbor < "$TEST_HOME/bytes.cbor")" ]]
[[ "$bytesDrv" = "$(nix derivation add --cbor --dry-run < "$TEST_HOME/bytes.cbor")" ]]
nix derivation show --cbor "$bytesDrv" > "$TEST_HOME/bytes-roundtrip.cbor"
cmp "$TEST_HOME/bytes.cbor" "$TEST_HOME/bytes-roundtrip.cbor"

# Derivation is input addressed, all outputs have a path
jq -e '.outputs | .[] | has("path")' < "$TEST_HOME/simple.json"

# Input addressed derivations cannot be renamed.
jq '.name = "foo"' < "$TEST_HOME/simple.json" | expectStderr 1 nix derivation add | grepQuiet "has incorrect output"

# If we remove the input addressed to make it a deferred derivation, we
# still get the same result because Nix will see that need not be
# deferred and fill in the right input address for us.
drvPath3=$(jq '.outputs |= map_values(del(.path))' < "$TEST_HOME/simple.json" | nix derivation add)
[[ "$drvPath" = "$drvPath3" ]]
