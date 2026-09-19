#!/usr/bin/env bash

source ./common.sh

cd "$TEST_ROOT"

checkRegistry() {
    local registryPath=$1
    local registryFile=$2

    nix registry add --registry "$registryPath" flake1 flake:target1
    nix registry add --registry "$registryPath" flake2 flake:target2
    [[ $(nix registry list --flake-registry "$registryFile" | grep '^global') == \
        $'global flake:flake1 flake:target1\nglobal flake:flake2 flake:target2' ]]

    nix registry remove --registry "$registryPath" flake1
    [[ $(nix registry list --flake-registry "$registryFile" | grep '^global') == \
        'global flake:flake2 flake:target2' ]]

    rm "$registryFile"
}

ln -s "$TEST_ROOT/custom-registry.json" absolute-link.json
ln -s custom-registry.json relative-link.json

for registryPath in "$TEST_ROOT/custom-registry.json" ./custom-registry.json custom-registry.json \
    ./absolute-link.json ./relative-link.json; do
    checkRegistry "$registryPath" "$TEST_ROOT/custom-registry.json"
done

mkdir -p nested/child
ln -s nested/child directory-link
checkRegistry ./directory-link/../custom-registry.json "$TEST_ROOT/nested/custom-registry.json"
