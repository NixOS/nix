#!/usr/bin/env bash

source common.sh

TODO_NixOS # auto-optimise-store is a restricted setting for non-trusted users

if ! isDaemonNewer "2.37.0pre20261121"; then
    expectStderr 1 nix store stats | grepQuiet "does not provide statistics"
    skipTest "Daemon does not support store stats"
fi

clearStoreIfPossible

# Build a path named $1 holding one file of $2 spaces.
mkFile() {
    # shellcheck disable=SC2016
    echo 'with import '"${config_nix}"'; mkDerivation { name = "'"$1"'"; builder = builtins.toFile "builder" "mkdir $out; printf %'"$2"'s > $out/f"; }' \
        | nix-build - --no-out-link "${@:3}"
}

# a1 and a2 are hard-linked through `.links`; b has their contents but is
# not linked. c1 and c2 share contents that `.links` does not have yet. d1
# and d2 are one inode, so linking it once covers both.
mkFile a1 5000 --auto-optimise-store > /dev/null
mkFile a2 5000 --auto-optimise-store > /dev/null
mkFile b 5000 > /dev/null
mkFile c1 3000 > /dev/null
mkFile c2 3000 > /dev/null
d1=$(mkFile d1 1000)
d2=$(mkFile d2 1000)
chmod u+w "$d2"
ln -f "$d1/f" "$d2/f"
chmod u-w "$d2"

stats=$(nix store stats --json)
[[ $(jq -c .predictedDedup <<< "$stats") = '{"bytes":8000,"files":2}' ]]
[[ $(jq -c .dedup <<< "$stats") = '{"bytes":5000,"files":1}' ]]
[[ $(jq .pathCount <<< "$stats") = $(nix path-info --all | wc -l) ]]

human=$(nix store stats 2>&1)
[[ $human = *"Valid paths: $(jq .pathCount <<< "$stats")"* ]]
[[ $human = *"Saved by hard-linking: 4.9 KiB (1 files)"* ]]
# shellcheck disable=SC2016
[[ $human = *'Would be saved by `nix store optimise`: 7.8 KiB (2 files)'* ]]

# After `nix store optimise`, every copy is a hard link.
nix store optimise
stats=$(nix store stats --json)
[[ $(jq -c .predictedDedup <<< "$stats") = '{"bytes":0,"files":0}' ]]
[[ $(jq -c .dedup <<< "$stats") = '{"bytes":14000,"files":4}' ]]

# The daemon passes the statistics through.
startDaemon
[[ $(nix store stats --json) = "$stats" ]]
killDaemon

# A daemon whose store has no statistics.
expectStderr 1 nix --store "ssh-ng://localhost?remote-program=$(readlink -e ./common/nix3-daemon-stdio.sh)&remote-store=dummy://" store stats \
    | grepQuiet "does not provide statistics"
