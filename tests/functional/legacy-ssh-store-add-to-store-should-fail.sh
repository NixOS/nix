#!/usr/bin/env bash

source common.sh
source legacy-ssh-store-add-to-store-common.sh

out=$(legacySshAddToStoreFailureOutput)

echo "$out" | grepQuiet "importing paths is not allowed"
echo "$out" | grepQuiet "cannot add path '/nix/store/example' because it lacks a signature by a trusted key"
echo "$out" | grepQuiet "failed to add path"
echo "$out" | grepQuiet "remote returned text where a binary reply was expected"
echo "$out" | grepQuietInverse "serialised integer"
