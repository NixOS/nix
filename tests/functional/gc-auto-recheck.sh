#!/usr/bin/env bash

source common.sh

needLocalStore '"min-free" and "max-free" are daemon options'

TODO_NixOS

wait_for_log() {
    local pattern=$1
    local log=$2

    for _ in {1..100}; do
        if [[ -e "$log" ]] && grepQuiet "$pattern" "$log"; then
            return 0
        fi
        sleep 0.1
    done

    return 1
}

terminate_lock_holder() {
    local pid=$1

    if ! kill -0 "$pid" 2> /dev/null; then
        return 0
    fi

    kill "$pid" 2> /dev/null || return 0

    if wait "$pid"; then
        return 0
    else
        local status=$?
        if [[ "$status" -ne 143 ]]; then
            echo "GC lock holder exited with status $status while being terminated" >&2
        fi
        return 0
    fi
}

lock_holder_pid=

cleanup_lock_holder() {
    if [[ -n "$lock_holder_pid" ]]; then
        terminate_lock_holder "$lock_holder_pid"
        lock_holder_pid=
    fi
}

start_gc_lock_holder() {
    local gc_sync=$1
    local lock_holder_log=$2

    mkfifo "$gc_sync"

    _NIX_TEST_GC_SYNC_2=$gc_sync nix-store --gc --print-dead -vvvvv > "$lock_holder_log" 2>&1 &
    lock_holder_pid=$!

    if ! wait_for_log "finding garbage collector roots" "$lock_holder_log"; then
        terminate_lock_holder "$lock_holder_pid"
        lock_holder_pid=
        fail "GC lock holder did not reach root traversal"
    fi
}

wait_for_auto_gc_start() {
    local auto_gc_log=$1
    local auto_gc_pid=$2
    local gc_sync=$3

    if ! wait_for_log "running auto-GC" "$auto_gc_log"; then
        printf 'unlock\n' > "$gc_sync"
        wait "$lock_holder_pid"
        lock_holder_pid=
        if wait "$auto_gc_pid"; then
            :
        else
            local auto_gc_status=$?
            echo "auto-GC build exited with status $auto_gc_status" >&2
        fi
        fail "auto-GC did not start while the global GC lock was held"
    fi
}

write_fake_free() {
    local bytes=$1

    echo "$bytes" > "$fake_free".tmp
    mv "$fake_free".tmp "$fake_free"
}

trap cleanup_lock_holder EXIT

fake_free=$TEST_ROOT/fake-free
export _NIX_TEST_FREE_SPACE_FILE=$fake_free

expr=$(cat <<EOF
with import ${config_nix}; mkDerivation {
  name = "gc-recheck-after-lock";
  buildCommand = ''
    set -x
    mkdir \$out
    echo foo > \$out/bar
  '';
}
EOF
)

clearStore
write_fake_free 100

nix store add-path --name garbage-recovered ./nar-access.sh > /dev/null

reserved_path=$TEST_ROOT/var/nix/db/reserved
[[ -f "$reserved_path" ]]

gc_sync=$TEST_ROOT/gc-sync-recovered
lock_holder_log=$TEST_ROOT/gc-lock-holder-recovered.log

start_gc_lock_holder "$gc_sync" "$lock_holder_log"

auto_gc_log=$TEST_ROOT/auto-gc-recheck-recovered.log
nix build --impure -vvvvv -o "$TEST_ROOT"/result-recovered --expr "$expr" \
    --min-free 1K --max-free 2K --min-free-check-interval 0 > "$auto_gc_log" 2>&1 &
auto_gc_pid=$!

wait_for_auto_gc_start "$auto_gc_log" "$auto_gc_pid" "$gc_sync"

write_fake_free 4096
printf 'unlock\n' > "$gc_sync"
wait "$lock_holder_pid"
lock_holder_pid=
wait "$auto_gc_pid"

grepQuiet "skipping auto-GC because available space is now" "$auto_gc_log"
grepQuietInverse "finding garbage collector roots" "$auto_gc_log"
[[ -f "$reserved_path" ]]
[[ foo = $(cat "$TEST_ROOT"/result-recovered/bar) ]]

clearStore
write_fake_free 100

nix store add-path --name garbage-still-low ./nar-access.sh > /dev/null
[[ -f "$reserved_path" ]]

gc_sync=$TEST_ROOT/gc-sync-still-low
lock_holder_log=$TEST_ROOT/gc-lock-holder-still-low.log

start_gc_lock_holder "$gc_sync" "$lock_holder_log"

auto_gc_log=$TEST_ROOT/auto-gc-recheck-still-low.log
nix build --impure -vvvvv -o "$TEST_ROOT"/result-still-low --expr "$expr" \
    --min-free 3K --max-free 4K --min-free-check-interval 0 > "$auto_gc_log" 2>&1 &
auto_gc_pid=$!

wait_for_auto_gc_start "$auto_gc_log" "$auto_gc_pid" "$gc_sync"

write_fake_free 2048
printf 'unlock\n' > "$gc_sync"
wait "$lock_holder_pid"
lock_holder_pid=
wait "$auto_gc_pid"

grepQuiet "continuing auto-GC to free 2048 bytes" "$auto_gc_log"
grepQuiet "finding garbage collector roots" "$auto_gc_log"
[[ foo = $(cat "$TEST_ROOT"/result-still-low/bar) ]]
