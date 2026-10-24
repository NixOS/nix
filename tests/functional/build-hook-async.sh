#!/usr/bin/env bash

# The worker runs other goals while one waits for the build hook's reply.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

requireDaemonIsThisVersion
TODO_NixOS

hook="$TEST_ROOT/hook.sh"
marker="$TEST_ROOT/local-built"
met="$TEST_ROOT/hook-met"
rm -f "$marker" "$met"

# `exec cat` drains the input so Nix's writes do not hit EPIPE.
cat > "$hook" <<EOF
#!/bin/sh
i=0
while [ ! -e "$marker" ]; do
  i=\$((i + 1))
  if [ "\$i" -ge 300 ]; then
    echo timeout >> "$met"
    echo "# decline-permanently" >&2
    exec cat > /dev/null
  fi
  sleep 0.1
done
echo ok >> "$met"
echo "# decline-permanently" >&2
exec cat > /dev/null
EOF
chmod +x "$hook"

echo "build-hook = $hook" >> "$test_nix_conf"
restartDaemon

# shellcheck disable=SC2016
nix-build --max-jobs 2 --no-out-link -E '
  with import '"${config_nix}"';
  let
    dep = mkDerivation {
      name = "async-hook-dep";
      preferLocalBuild = true;
      buildCommand = "sleep 1; echo > $out";
    };
  in
  [
    (mkDerivation { name = "async-hook-remote"; buildCommand = "echo > $out"; })
    (mkDerivation {
      name = "async-hook-local";
      preferLocalBuild = true;
      inherit dep;
      buildCommand = "touch '"$marker"'; echo > $out";
    })
  ]
'

[[ $(< "$met") = ok ]]
