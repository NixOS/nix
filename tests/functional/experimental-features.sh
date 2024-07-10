#!/usr/bin/env bash

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
#     nix --experimental-features '' "$@" | grepQuietInverse flake
#     nix --experimental-features '' "$@" | grepQuiet flake
#
#     # Also, the order should not matter
#     nix "$@" --experimental-features '' | grepQuietInverse flake
#     nix "$@" --experimental-features '' | grepQuiet flake
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

xpFeature=auto-allocate-uids
gatedSetting=auto-allocate-uids

# Experimental feature is disabled before, ignore and warn.
NIX_CONFIG="
  experimental-features =
  $gatedSetting = true
" expect 1 nix config show $gatedSetting 1>"$TEST_ROOT"/stdout 2>"$TEST_ROOT"/stderr
[[ $(cat "$TEST_ROOT/stdout") = '' ]]
grepQuiet "error: could not find setting '$gatedSetting'" "$TEST_ROOT/stderr"

# Experimental feature is disabled after, ignore and warn.
NIX_CONFIG="
  $gatedSetting = true
  experimental-features =
" expect 1 nix config show $gatedSetting 1>"$TEST_ROOT"/stdout 2>"$TEST_ROOT"/stderr
[[ $(cat "$TEST_ROOT/stdout") = '' ]]
grepQuiet "error: could not find setting '$gatedSetting'" "$TEST_ROOT/stderr"

# Experimental feature is enabled before, process.
NIX_CONFIG="
  experimental-features = $xpFeature
  $gatedSetting = true
" nix config show $gatedSetting 1>"$TEST_ROOT"/stdout 2>"$TEST_ROOT"/stderr
grepQuiet "true" "$TEST_ROOT/stdout"

# Experimental feature is enabled after, process.
NIX_CONFIG="
  $gatedSetting = true
  experimental-features = $xpFeature
" nix config show $gatedSetting 1>"$TEST_ROOT"/stdout 2>"$TEST_ROOT"/stderr
grepQuiet "true" "$TEST_ROOT/stdout"
grepQuietInverse "Ignoring setting '$gatedSetting'" "$TEST_ROOT/stderr"

function exit_code_both_ways {
    expect 1 nix --experimental-features '' "$@" 1>/dev/null
    nix --experimental-features "$xpFeature" "$@" 1>/dev/null

    # Also, the order should not matter
    expect 1 nix "$@" --experimental-features '' 1>/dev/null
    nix "$@" --experimental-features "$xpFeature" 1>/dev/null
}

exit_code_both_ways config show --auto-allocate-uids

# Double check these are stable
nix --experimental-features '' --help 1>/dev/null
nix --experimental-features '' doctor --help 1>/dev/null
nix --experimental-features '' repl --help 1>/dev/null
nix --experimental-features '' upgrade-nix --help 1>/dev/null
