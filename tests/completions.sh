source common.sh

cd "$TEST_ROOT"

mkdir -p dep && pushd dep
cat <<EOF > flake.nix
{
    outputs = i: { };
}
EOF
popd
mkdir -p foo && pushd foo
cat <<EOF > flake.nix
{
    inputs.a.url = "path:$(realpath ../dep)";

    outputs = i: {
        sampleOutput = 1;
    };
}
EOF

popd

# Test the completion of a subcommand
[[ "$(NIX_GET_COMPLETIONS=1 nix buil)" == $'normal\nbuild\t' ]]
[[ "$(NIX_GET_COMPLETIONS=2 nix flake metad)" == $'normal\nmetadata\t' ]]

# Filename completion
[[ "$(NIX_GET_COMPLETIONS=2 nix build ./f)" == $'filenames\n./foo\t' ]]
[[ "$(NIX_GET_COMPLETIONS=2 nix build ./nonexistent)" == $'filenames' ]]

# Input override completion
[[ "$(NIX_GET_COMPLETIONS=4 nix build ./foo --override-input '')" == $'normal\na\t' ]]

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
