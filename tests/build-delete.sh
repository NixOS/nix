source common.sh

clearStore

set -o pipefail

# https://github.com/NixOS/nix/issues/6572
issue_6572_independent_outputs() {
    nix build -f multiple-outputs.nix --json independent --no-link > $TEST_ROOT/independent.json

    # Make sure that 'nix build' can build a derivation that depends on both outputs of another derivation.
    p=$(nix build -f multiple-outputs.nix use-independent --no-link --print-out-paths)
    nix-store --delete "$p" # Clean up for next test

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    nix-store --delete "$(jq -r <$TEST_ROOT/independent.json .[0].outputs.first)"
    p=$(nix build -f multiple-outputs.nix use-independent --no-link --print-out-paths)
    cmp $p <<EOF
first
second
EOF
    nix-store --delete "$p" # Clean up for next test

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    nix-store --delete "$(jq -r <$TEST_ROOT/independent.json .[0].outputs.second)"
    p=$(nix build -f multiple-outputs.nix use-independent --no-link --print-out-paths)
    cmp $p <<EOF
first
second
EOF
    nix-store --delete "$p" # Clean up for next test
}
issue_6572_independent_outputs


# https://github.com/NixOS/nix/issues/6572
issue_6572_dependent_outputs() {

    nix build -f multiple-outputs.nix --json a --no-link > $TEST_ROOT/a.json

    # # Make sure that 'nix build' can build a derivation that depends on both outputs of another derivation.
    p=$(nix build -f multiple-outputs.nix use-a --no-link --print-out-paths)
    nix-store --delete "$p" # Clean up for next test

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    nix-store --delete "$(jq -r <$TEST_ROOT/a.json .[0].outputs.second)"
    p=$(nix build -f multiple-outputs.nix use-a --no-link --print-out-paths)
    cmp $p <<EOF
first
second
EOF
    nix-store --delete "$p" # Clean up for next test
}
if isDaemonNewer "2.12pre0"; then
    issue_6572_dependent_outputs
fi
