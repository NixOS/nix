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

# Test settings that are gated on experimental features; the setting is ignored
# with a warning if the experimental feature is not enabled. The order of the
# `setting = value` lines in the configuration should not matter.

# 'flakes' experimental-feature is disabled before, ignore and warn
NIX_CONFIG='
  experimental-features = nix-command
  accept-flake-config = true
' nix show-config accept-flake-config 1>$TEST_ROOT/stdout 2>$TEST_ROOT/stderr
grepQuiet "false" $TEST_ROOT/stdout
grepQuiet "Ignoring setting 'accept-flake-config' because experimental feature 'flakes' is not enabled" $TEST_ROOT/stderr

# 'flakes' experimental-feature is disabled after, ignore and warn
NIX_CONFIG='
  accept-flake-config = true
  experimental-features = nix-command
' nix show-config accept-flake-config 1>$TEST_ROOT/stdout 2>$TEST_ROOT/stderr
grepQuiet "false" $TEST_ROOT/stdout
grepQuiet "Ignoring setting 'accept-flake-config' because experimental feature 'flakes' is not enabled" $TEST_ROOT/stderr

# 'flakes' experimental-feature is enabled before, process
NIX_CONFIG='
  experimental-features = nix-command flakes
  accept-flake-config = true
' nix show-config accept-flake-config 1>$TEST_ROOT/stdout 2>$TEST_ROOT/stderr
grepQuiet "true" $TEST_ROOT/stdout
grepQuietInverse "Ignoring setting 'accept-flake-config'" $TEST_ROOT/stderr

# 'flakes' experimental-feature is enabled after, process
NIX_CONFIG='
  accept-flake-config = true
  experimental-features = nix-command flakes
' nix show-config accept-flake-config 1>$TEST_ROOT/stdout 2>$TEST_ROOT/stderr
grepQuiet "true" $TEST_ROOT/stdout
grepQuietInverse "Ignoring setting 'accept-flake-config'" $TEST_ROOT/stderr

function exit_code_both_ways {
    expect 1 nix --experimental-features 'nix-command' "$@" 1>/dev/null
    nix --experimental-features 'nix-command flakes' "$@" 1>/dev/null

    # Also, the order should not matter
    expect 1 nix "$@" --experimental-features 'nix-command' 1>/dev/null
    nix "$@" --experimental-features 'nix-command flakes' 1>/dev/null
}

exit_code_both_ways show-config --flake-registry 'https://no'

# Double check these are stable
nix --experimental-features '' --help 1>/dev/null
nix --experimental-features '' doctor --help 1>/dev/null
nix --experimental-features '' repl --help 1>/dev/null
nix --experimental-features '' upgrade-nix --help 1>/dev/null

# These 3 arguments are currently given to all commands, which is wrong (as not
# all care). To deal with fixing later, we simply make them require the
# nix-command experimental features --- it so happens that the commands we wish
# stabilizing to do not need them anyways.
for arg in '--offline' '--refresh'; do
    nix --experimental-features 'nix-command' "$arg" --help 1>/dev/null
    expect 1 nix --experimental-features '' "$arg" --help 1>/dev/null
done
