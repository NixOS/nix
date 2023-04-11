source common.sh

# Skipping these two for now, because we actually *do* want flags and
# config settings to always show up in the manual, just be marked
# experimental. Will reenable once the manual generation takes advantage
# of the JSON metadata on this.
#
# # Without flakes, flake options should not show up
# # With flakes, flake options should show up
#
# function grep_both_ways {
#     nix --experimental-features 'nix-command' "$@" | grepQuietInverse flake
#     nix --experimental-features 'nix-command flakes' "$@" | grepQuiet flake
#
#     # Also, the order should not matter
#     nix "$@" --experimental-features 'nix-command' | grepQuietInverse flake
#     nix "$@" --experimental-features 'nix-command flakes' | grepQuiet flake
# }
#
# # Simple case, the configuration effects the running command
# grep_both_ways show-config
#
# # Medium case, the configuration effects --help
# grep_both_ways store gc --help

expect 1 nix --experimental-features 'nix-command' show-config --flake-registry 'https://no'
nix --experimental-features 'nix-command flakes' show-config --flake-registry 'https://no'

# Double check these are stable
nix --experimental-features '' --help
nix --experimental-features '' doctor --help
nix --experimental-features '' repl --help
nix --experimental-features '' upgrade-nix --help

# These 3 arguments are currently given to all commands, which is wrong (as not
# all care). To deal with fixing later, we simply make them require the
# nix-command experimental features --- it so happens that the commands we wish
# stabilizing to do not need them anyways.
for arg in '--print-build-logs' '--offline' '--refresh'; do
    nix --experimental-features 'nix-command' "$arg" --help
    ! nix --experimental-features '' "$arg" --help
done
