#!/usr/bin/env bash

# The copy-back into a local client store runs while the goal holds the
# outputs' path locks, so it must skip them or `flock` hangs the process.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

client="$TEST_ROOT/client"
log="$TEST_ROOT/log"
chmod -R +w "$client" 2> /dev/null || true
rm -rf "$client"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

cat > "$TEST_ROOT/local-client.nix" <<'NIX'
{ configNix }:
with import configNix;
rec {
  multi = mkDerivation {
    name = "inprocess-local-client-multi";
    outputs = [ "out" "dev" "doc" "man" ];
    buildCommand = ''
      echo inprocess-local-client-multi-was-here
      mkdir $out $dev $doc $man
      echo out > $out/f
      echo dev > $dev/f
      echo doc > $doc/f
      echo man > $man/f
    '';
  };
  top = mkDerivation {
    name = "inprocess-local-client-top";
    buildCommand = ''
      echo inprocess-local-client-top-was-here
      cat ${multi.dev}/f ${multi.man}/f > $out
    '';
  };
  all = multi.all ++ [ top ];
}
NIX

outPaths=$(timeout 200 nix-build "$TEST_ROOT/local-client.nix" -A all \
  --argstr configNix "$config_nix" \
  --store "$client" --builders "ssh-ng://localhost - - 1 1" -j0 \
  --no-out-link -vvv 2> "$log")

[[ $(wc -l <<< "$outPaths") -eq 5 ]] || fail "expected five output paths, got: $outPaths"
for outPath in $outPaths; do
    nix path-info --store "$client" "$outPath"
done
topOut=$(nix-build "$TEST_ROOT/local-client.nix" -A top --argstr configNix "$config_nix" \
  --store "$client" -j0 --no-out-link)
[[ $(nix store cat --store "$client" "$topOut") == $'dev\nman' ]]

for name in multi top; do
    drvPath=$(nix-instantiate "$TEST_ROOT/local-client.nix" -A "$name" \
      --argstr configNix "$config_nix" --store "$client")
    nix log --store "$client" "$drvPath" | grepQuiet "inprocess-local-client-$name-was-here"
done

grepQuiet "building '.*-inprocess-local-client-multi.drv' on 'ssh-ng://localhost'" "$log"
grepQuiet "building '.*-inprocess-local-client-top.drv' on 'ssh-ng://localhost'" "$log"
[[ $(grep -c "remote build done" "$log") -eq 2 ]]
grepQuietInverse "starting build hook '" "$log"
