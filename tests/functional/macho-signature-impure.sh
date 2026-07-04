#!/usr/bin/env bash

# The impure-output shape of the signature repair: impure outputs
# are relocated into a daemon-private 0700 temporary directory before
# registration, so when a signed binary there needs its cross-output
# reference rewritten, the hook (running as the build user) must be
# given access to that directory. Exercises the chown-the-parent
# branch of the hook invocation.

source common.sh

TODO_NixOS # Requires clearing the store

[[ $(uname) == Darwin ]] || skipTest "Mach-O binaries can only be built on darwin"
[[ -x /usr/bin/cc ]] || skipTest "Need /usr/bin/cc to build the test fixture"
[[ -x /usr/bin/codesign ]] || skipTest "Need /usr/bin/codesign to validate signatures"

enableFeatures "ca-derivations impure-derivations"
restartDaemon

clearStore

# The default hook repairs the signature inside the temp dir; the
# registered binary runs and carries a valid signature.
json=$(nix build -L --no-link --json --file ./macho-signature-impure.nix)
dev=$(echo "$json" | jq -r '.[0].outputs.dev')
out=$(echo "$json" | jq -r '.[0].outputs.out')
[[ -x "$dev/bin/hello" ]]
/usr/bin/codesign --verify "$dev/bin/hello"
"$dev/bin/hello" | grepQuiet "^out=$out/data"

# With the hook disabled, the same build is refused.
clearStore
expectStderr 1 nix build -L --no-link --file ./macho-signature-impure.nix \
    --option macho-signature-repair-hook "" |
    grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file"
