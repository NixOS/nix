#!/usr/bin/env bash

source common.sh

TODO_NixOS
requireDaemonNewerThan "2.36.0pre"

export NIX_REMOTE=local

cat > "$TEST_ROOT/paths.nix" <<'EOF'
{ config }:
with config;
rec {
  duplicates = mkDerivation {
    name = "import-duplicates";
    buildCommand = ''
      mkdir "$out"
      for ((i = 0; i < 4096; i++)); do
        echo duplicate-content > "$out/$i"
      done
    '';
  };
  payload = mkDerivation {
    name = "import-payload";
    buildCommand = ''
      mkdir "$out"
      echo ${duplicates} > "$out/reference"
      dd if=/dev/zero of="$out/data" bs=1048576 count=8
    '';
  };
}
EOF

duplicates=$(nix-build "$TEST_ROOT/paths.nix" \
    --arg config "import $config_nix" -A duplicates --no-out-link)
payload=$(nix-build "$TEST_ROOT/paths.nix" \
    --arg config "import $config_nix" -A payload --no-out-link)

nix-store --query --references "$payload" | grepQuiet -Fx "$duplicates"

daemon=$(PATH="$DAEMON_PATH" command -v nix-daemon)
wrapper="$TEST_ROOT/import-daemon"

copyBatch() {
    local name=$1 optimise=$2 verbose=$3
    local root="$TEST_ROOT/$name"

    local destination="$root?store=$NIX_STORE_DIR&real=$root$NIX_STORE_DIR"
    local remote="ssh-ng://localhost?store=$NIX_STORE_DIR&remote-store=$root%3fstore=$NIX_STORE_DIR%26real=$root$NIX_STORE_DIR&remote-program=$wrapper"
    local log="$TEST_ROOT/$name.log"

    {
        printf '#!%s\n' "$SHELL"
        printf 'exec %q --option auto-optimise-store %q ' "$daemon" "$optimise"
        if [[ $verbose == true ]]; then
            printf '%s ' '-v'
        fi
        printf '"$@"\n'
    } > "$wrapper"
    chmod +x "$wrapper"

    local status=0
    timeout --kill-after=5 120 nix copy -v --no-check-sigs --to "$remote" \
        "$payload" "$duplicates" > "$log" 2>&1 || status=$?
    if [[ $status != 0 ]]; then
        tail -n 40 "$log" >&2
        fail "$name: batch import failed (exit $status)"
    fi

    local invalid
    invalid=$(nix-store --store "$destination" --check-validity \
        --print-invalid "$duplicates" "$payload")
    [[ -z $invalid ]]

    if [[ $optimise == true ]]; then
        [[ "$root$duplicates/0" -ef "$root$duplicates/1" ]]
    else
        [[ ! "$root$duplicates/0" -ef "$root$duplicates/1" ]]
    fi

    if [[ $optimise == true && $verbose == true ]]; then
        grepQuiet 'linking .*\.links/' "$log"
    else
        grepQuietInverse 'linking .*\.links/' "$log"
    fi
}

copyBatch quiet-optimise true false
copyBatch verbose-no-optimise false true
copyBatch verbose-optimise true true