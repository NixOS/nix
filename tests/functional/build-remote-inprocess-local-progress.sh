#!/usr/bin/env bash

# A remote build in flight neither blocks the worker nor counts in `--max-jobs`.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion
requireSandboxSupport
requiresUnprivilegedUserNamespaces
[[ "${busybox-}" =~ busybox ]] || skipTest "no busybox"

# Avoid store dir being inside sandbox build-dir.
unset NIX_STORE_DIR

chmod -R +w "$TEST_ROOT/machine"* 2> /dev/null || true
rm -rf "$TEST_ROOT/machine"*

sync="$TEST_ROOT/sync"
log="$TEST_ROOT/log"
rm -rf "$sync"
mkdir -p "$sync"
mkfifo "$sync/remote-started" "$sync/local-done"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

echo "extra-sandbox-paths = $sync" >> "$test_nix_conf"

# Only shell builtins, as the sandbox shell has no other applets.
cat > "$TEST_ROOT/local-progress.nix" <<'NIX'
{ configNix, sync, busybox }:
let
  system = (import configNix).system;
  mk = name: attrs: derivation ({
    inherit name system;
    builder = busybox;
    args = [ "sh" "-e" "-c" attrs.script ];
  } // removeAttrs attrs [ "script" ]);
  remote = mk "inprocess-remote-blocked" {
    requiredSystemFeatures = [ "remote-only" ];
    script = ''
      echo started > ${sync}/remote-started
      read d < ${sync}/local-done
      echo R > $out
    '';
  };
  local1 = mk "inprocess-local-1" {
    preferLocalBuild = true;
    script = ''
      read started < ${sync}/remote-started
      echo L1 > $out
    '';
  };
  local2 = mk "inprocess-local-2" {
    preferLocalBuild = true;
    script = ''
      read x < ${local1}
      echo done > ${sync}/local-done
      echo $x-L2 > $out
    '';
  };
in
mk "inprocess-top" {
  preferLocalBuild = true;
  script = ''
    read r < ${remote}
    read l < ${local2}
    echo $r $l > $out
  '';
}
NIX

machine="ssh-ng://localhost?remote-store=$TEST_ROOT/machine1?system-features=remote-only - - 1 1 remote-only"

outPath=$(timeout 240 nix-build "$TEST_ROOT/local-progress.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" --argstr busybox "$busybox" \
  --store "$TEST_ROOT/machine0" --builders "$machine" --max-jobs 1 \
  --no-out-link -vvv 2> "$log")

grep 'R L1-L2' "$TEST_ROOT/machine0/$outPath"

output=$(nix path-info --store "$TEST_ROOT/machine1" --all)
echo "$output" | grepQuiet 'inprocess-remote-blocked'
echo "$output" | grepQuietInverse 'inprocess-local-'
echo "$output" | grepQuietInverse 'inprocess-top'

grepQuiet "building '.*-inprocess-remote-blocked.drv' on 'ssh-ng://localhost" "$log"
grepQuiet "remote build done" "$log"
grepQuietInverse "starting build hook '" "$log"
