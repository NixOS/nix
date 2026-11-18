#!/usr/bin/env bash

source common.sh

clearStore

expr='{ system, structured ? false }: derivation {
  inherit system;
  name = "test";
  builder = "/dev/null";
  __structuredAttrs = structured;
}'

assertNoDrvWritten() {
    [[ -z $(find "$NIX_STORE_DIR" -name '*-test.drv') ]]
}

validDrvPath=$(nix-instantiate --expr "$expr" --argstr system 'a b')
nix derivation show "$validDrvPath" | jq '.derivations[] | .outputs |= map_values(del(.path))' > "$TEST_HOME/valid.json"
clearStore

for system in '\' '"' 'a\b' 'a"b'; do
    expectStderr 1 nix-instantiate --expr "$expr" --argstr system "$system" \
        | grepQuiet "must not contain"
    assertNoDrvWritten

    expectStderr 1 nix-instantiate --expr "$expr" --argstr system "$system" --arg structured true \
        | grepQuiet "must not contain"
    assertNoDrvWritten

    expectStderr 1 nix-instantiate --readonly-mode --expr "$expr" --argstr system "$system" \
        | grepQuiet "must not contain"

    jq --arg system "$system" '.system = $system | .env.system = $system' "$TEST_HOME/valid.json" > "$TEST_HOME/invalid.json"

    expectStderr 1 nix derivation add < "$TEST_HOME/invalid.json" \
        | grepQuiet "must not contain"
    assertNoDrvWritten

    expectStderr 1 nix derivation add --dry-run < "$TEST_HOME/invalid.json" \
        | grepQuiet "must not contain"
done

for system in 'a b' $'a\nb' "a'b"; do
    drvPath=$(nix-instantiate --expr "$expr" --argstr system "$system")
    [[ $(nix derivation show "$drvPath" | jq -r '.derivations[].system') == "$system" ]]
done
