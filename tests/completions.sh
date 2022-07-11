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
[[ $(printf "normal\nbuild\t\n") == $(NIX_GET_COMPLETIONS=1 nix buil) ]]
[[ $(printf "normal\nmetadata\t\n") == $(NIX_GET_COMPLETIONS=2 nix flake metad) ]]

# Filename completion
[[ $(printf "filenames\n./foo\t\n") == $(NIX_GET_COMPLETIONS=2 nix build ./f) ]]
[[ $(printf "filenames\n") == $(NIX_GET_COMPLETIONS=2 nix build ./nonexistent) ]]

# Input override completion
[[ $(printf "normal\na\t\n") == $(NIX_GET_COMPLETIONS=4 nix build ./foo --override-input '') ]]

# Cli flag completion
NIX_GET_COMPLETIONS=2 nix build --log-form | grep -- "--log-format"

# Config option completion
## With `--option`
NIX_GET_COMPLETIONS=3 nix build --option allow-import-from | grep -- "allow-import-from-derivation"
## As a cli flag – not working atm
# NIX_GET_COMPLETIONS=2 nix build --allow-import-from | grep -- "allow-import-from-derivation"


# Attr path completions
[[ $(printf "attrs\n./foo#sampleOutput\t\n") == $(NIX_GET_COMPLETIONS=2 nix eval ./foo\#sam) ]]
[[ $(printf "attrs\noutputs\t\n") == $(NIX_GET_COMPLETIONS=4 nix eval --file ./foo/flake.nix outp) ]]
