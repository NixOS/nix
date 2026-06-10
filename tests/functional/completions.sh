#!/usr/bin/env bash

source common.sh

cd "$TEST_ROOT"

mkdir -p dep
cat <<EOF > dep/flake.nix
{
    outputs = i: { };
}
EOF
mkdir -p foo
cat <<EOF > foo/flake.nix
{
    inputs.a.url = "path:$(realpath dep)";

    outputs = i: {
        sampleOutput = 1;
    };
}
EOF
mkdir -p bar
cat <<EOF > bar/flake.nix
{
    inputs.b.url = "path:$(realpath dep)";

    outputs = i: {
        sampleOutput = 1;
    };
}
EOF
mkdir -p err
cat <<EOF > err/flake.nix
throw "error"
EOF

# Test the completion of a subcommand
[[ "$(NIX_GET_COMPLETIONS=1 nix buil)" == $'normal\nbuild\t' ]]
[[ "$(NIX_GET_COMPLETIONS=2 nix flake metad)" == $'normal\nmetadata\t' ]]

# Filename completion
[[ "$(NIX_GET_COMPLETIONS=2 nix build ./f)" == $'filenames\n./foo\t' ]]
[[ "$(NIX_GET_COMPLETIONS=2 nix build ./nonexistent)" == $'filenames' ]]

# Input override completion
[[ "$(NIX_GET_COMPLETIONS=4 nix build ./foo --override-input '')" == $'normal\na\t' ]]
[[ "$(NIX_GET_COMPLETIONS=5 nix flake show ./foo --override-input '')" == $'normal\na\t' ]]
cd ./foo
[[ "$(NIX_GET_COMPLETIONS=3 nix flake update '')" == $'normal\na\t' ]]
cd ..
[[ "$(NIX_GET_COMPLETIONS=5 nix flake update --flake './foo' '')" == $'normal\na\t' ]]
## With multiple input flakes
[[ "$(NIX_GET_COMPLETIONS=5 nix build ./foo ./bar --override-input '')" == $'normal\na\t\nb\t' ]]
## With tilde expansion
# shellcheck disable=SC2088
[[ "$(HOME=$PWD NIX_GET_COMPLETIONS=4 nix build '~/foo' --override-input '')" == $'normal\na\t' ]]
# shellcheck disable=SC2088
[[ "$(HOME=$PWD NIX_GET_COMPLETIONS=5 nix flake update --flake '~/foo' '')" == $'normal\na\t' ]]
## Out of order
[[ "$(NIX_GET_COMPLETIONS=3 nix build --override-input '' '' ./foo)" == $'normal\na\t' ]]
[[ "$(NIX_GET_COMPLETIONS=4 nix build ./foo --override-input '' '' ./bar)" == $'normal\na\t\nb\t' ]]

# Cli flag completion
NIX_GET_COMPLETIONS=2 nix build --log-form | grep -- "--log-format"

# Config option completion
## With `--option`
NIX_GET_COMPLETIONS=3 nix build --option allow-import-from | grep -- "allow-import-from-derivation"
## As a cli flag – not working atm
# NIX_GET_COMPLETIONS=2 nix build --allow-import-from | grep -- "allow-import-from-derivation"

# Attr path completions
[[ "$(NIX_GET_COMPLETIONS=2 nix eval ./foo\#sam)" == $'attrs\n./foo#sampleOutput\t' ]]
[[ "$(NIX_GET_COMPLETIONS=4 nix eval --file ./foo/flake.nix outp)" == $'attrs\noutputs\t' ]]
[[ "$(NIX_GET_COMPLETIONS=4 nix eval --file ./err/flake.nix outp 2>&1)" == $'attrs' ]]
[[ "$(NIX_GET_COMPLETIONS=2 nix eval ./err\# 2>&1)" == $'attrs' ]]
