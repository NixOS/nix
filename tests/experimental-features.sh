source common.sh

# Without flakes, flake options should not show up
# With flakes, flake options should show up

function both_ways {
    nix --experimental-features 'nix-command' "$@" | grepQuietInverse flake
    nix --experimental-features 'nix-command flakes' "$@" | grepQuiet flake

    # Also, the order should not matter
    nix "$@" --experimental-features 'nix-command' | grepQuietInverse flake
    nix "$@" --experimental-features 'nix-command flakes' | grepQuiet flake
}

# Simple case, the configuration effects the running command
both_ways show-config

# Complicated case, earlier args effect later args

both_ways store gc --help

expect 1 nix --experimental-features 'nix-command' show-config --flake-registry 'https://no'
nix --experimental-features 'nix-command flakes' show-config --flake-registry 'https://no'
