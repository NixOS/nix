#!/usr/bin/env bash

source common.sh

needLocalStore "optimises an isolated local store"
TODO_NixOS

paths=()
mkdir -p "$TEST_ROOT/roots"
for index in $(seq 1 32); do
    path="$NIX_STORE_DIR/$(printf '%032d' "$index")-parallel-$index"
    mkdir -p "$path/nested"
    printf 'parallel optimisation payload\n' > "$path/plain"
    truncate -s 1M "$path/plain"
    cp "$path/plain" "$path/nested/copy"
    cp "$path/plain" "$path/executable"
    chmod +x "$path/executable"
    printf '%s\n' "$index" > "$path/unique"
    ln -s missing-target "$path/symlink"
    ln -s missing-target "$path/nested/symlink-copy"
    ln -s another-target "$path/other-symlink"
    paths+=("$path")
done
topLevel="$NIX_STORE_DIR/99999999999999999999999999999999-top-level"
cp "${paths[0]}/plain" "$topLevel"
paths+=("$topLevel")

registerPaths() {
    for path in "${paths[@]}"; do
        printf '%s\n\n0\n' "$path"
    done | nix-store --register-validity --reregister
}

registerPaths
for index in "${!paths[@]}"; do
    nix-store --add-root "$TEST_ROOT/roots/$index" --realise "${paths[index]}"
done

inode() {
    stat --format=%i "$1"
}

plainHash=$(nix-hash --type sha256 --base32 "${paths[0]}/plain")

assertClean() {
    test -z "$(find "$NIX_STORE_DIR" -name '.tmp-link-*' -o -name 'tmp-*')"
    test -z "$(find "$NIX_STORE_DIR" -mindepth 1 -type d ! -path "$NIX_STORE_DIR/.links" -perm /222)"
}

if [[ -n "${OPTIMISE_TEST_PRELOAD:-}" ]]; then
    expect 120 timeout 5 env LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_TIMEOUT_MS=100 \
        NIX_TEST_OPTIMISE_LINK="$NIX_STORE_DIR/.links/$plainHash" \
        nix-store --optimise --option optimise-store-jobs 1
    nix-store --gc
fi

for jobs in 1 2 4 8 16; do
    if [[ "$jobs" != 1 ]]; then
        chmod -R u+w "$NIX_STORE_DIR"
        rm -rf "$NIX_STORE_DIR/.links"
        mkdir "$NIX_STORE_DIR/.links"
        for path in "${paths[@]}"; do
            if [[ -d "$path" ]]; then
                for file in plain nested/copy executable; do
                    cp --reflink=never "$path/$file" "$path/replacement"
                    mv "$path/replacement" "$path/$file"
                done
            else
                cp --reflink=never "$path" "$TEST_ROOT/replacement"
                mv "$TEST_ROOT/replacement" "$path"
            fi
        done
        registerPaths
    fi
    test "$(inode "${paths[0]}/plain")" != "$(inode "${paths[1]}/plain")"
    if [[ -n "${OPTIMISE_TEST_PRELOAD:-}" && "$jobs" != 1 ]]; then
        rm -f "$TEST_ROOT/collision"
        LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
            NIX_TEST_OPTIMISE_LINK="$NIX_STORE_DIR/.links/$plainHash" \
            NIX_TEST_OPTIMISE_COLLISION="$TEST_ROOT/collision" \
            nix-store --optimise --option optimise-store-jobs "$jobs"
        test -f "$TEST_ROOT/collision"
    else
        nix-store --optimise --option optimise-store-jobs "$jobs"
    fi
    plainInode=$(inode "${paths[0]}/plain")
    executableInode=$(inode "${paths[0]}/executable")
    test "$plainInode" != "$executableInode"
    for path in "${paths[@]:0:32}"; do
        test "$(inode "$path/plain")" = "$plainInode"
        test "$(inode "$path/nested/copy")" = "$plainInode"
        test "$(inode "$path/executable")" = "$executableInode"
        test -x "$path/executable"
        test ! -x "$path/plain"
        test "$(readlink "$path/symlink")" = missing-target
        test "$(readlink "$path/nested/symlink-copy")" = missing-target
        test "$(readlink "$path/other-symlink")" = another-target
        if [[ "$(uname)" == Linux ]]; then
            test "$(inode "$path/symlink")" = "$(inode "$path/nested/symlink-copy")"
        fi
    done
    test "$(inode "$topLevel")" = "$plainInode"
    nix-store --verify-path "${paths[@]}"
    nix-store --verify --check-contents
    nix-store --optimise --option optimise-store-jobs "$jobs" 2> "$TEST_ROOT/repeat.log"
    grepQuiet 'hard-linking 0 files' "$TEST_ROOT/repeat.log"
    test "$(inode "${paths[0]}/plain")" = "$plainInode"
    assertClean
done

hash=$(nix-hash --type sha256 --base32 "${paths[0]}/plain")
rm "$NIX_STORE_DIR/.links/$hash"
mkdir "$NIX_STORE_DIR/.links/$hash"
if nix-store --optimise --option optimise-store-jobs 4 > "$TEST_ROOT/error.log" 2>&1; then
    fail "optimisation unexpectedly accepted an invalid canonical link"
fi
grepQuiet 'error:' "$TEST_ROOT/error.log"
rmdir "$NIX_STORE_DIR/.links/$hash"
assertClean
nix-store --optimise --option optimise-store-jobs 4
nix-store --verify-path "${paths[@]}"

if [[ -n "${OPTIMISE_TEST_PRELOAD:-}" ]]; then
    path="${paths[0]}"
    chmod u+w "$path"
    cp --reflink=never "$path/plain" "$path/replacement"
    mv "$path/replacement" "$path/plain"
    chmod a-w "$path"
    if LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_RENAME_ERROR="$TEST_ROOT/rename-error" \
        nix-store --optimise --option optimise-store-jobs 4 > "$TEST_ROOT/rename-error.log" 2>&1; then
        fail "optimisation unexpectedly ignored a rename failure"
    fi
    test -f "$TEST_ROOT/rename-error"
    grepQuiet 'renaming' "$TEST_ROOT/rename-error.log"
    assertClean
    nix-store --verify-path "${paths[@]}"
fi

if [[ -n "${OPTIMISE_TEST_PRELOAD:-}" ]]; then
    for path in "${paths[@]:0:32}"; do
        chmod u+w "$path" "$path/nested"
        for file in plain nested/copy; do
            cp --reflink=never "$path/$file" "$path/replacement"
            mv "$path/replacement" "$path/$file"
        done
        chmod a-w "$path" "$path/nested"
    done
    rm "$NIX_STORE_DIR/.links/$plainHash"
    printf 'broken' > "$NIX_STORE_DIR/.links/$plainHash"
    LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_REPAIR_LINK="$NIX_STORE_DIR/.links/$plainHash" \
        NIX_TEST_OPTIMISE_REPAIR_COLLISION="$TEST_ROOT/repair-collision" \
        nix-store --optimise --option optimise-store-jobs 4
    test -f "$TEST_ROOT/repair-collision"
    test "$(nix-hash --type sha256 --base32 "$NIX_STORE_DIR/.links/$plainHash")" = "$plainHash"
    assertClean
    nix-store --verify-path "${paths[@]}"
    nix-store --optimise --option optimise-store-jobs 4
    canonicalInode=$(inode "$NIX_STORE_DIR/.links/$plainHash")
    for path in "${paths[@]:0:32}"; do
        test "$(inode "$path/plain")" = "$canonicalInode"
        test "$(inode "$path/nested/copy")" = "$canonicalInode"
    done

    first="$NIX_STORE_DIR/11111111111111111111111111111111-cache-first"
    anchor="$NIX_STORE_DIR/22222222222222222222222222222222-cache-anchor"
    cp "${paths[0]}/plain" "$first"
    cp "${paths[0]}/plain" "$anchor"
    ln "$anchor" "$TEST_ROOT/cache-alias"
    paths+=("$first" "$anchor")
    registerPaths
    LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_SWAP_LINK="$NIX_STORE_DIR/.links/$plainHash" \
        NIX_TEST_OPTIMISE_SWAP_BACKUP="$TEST_ROOT/old-canonical" \
        NIX_TEST_OPTIMISE_SWAP_SOURCE="$anchor" \
        NIX_TEST_OPTIMISE_SWAP_MARKER="$TEST_ROOT/swapped" \
        NIX_TEST_OPTIMISE_READ_GUARD="$anchor" \
        nix-store --optimise --option optimise-store-jobs 1
    test -f "$TEST_ROOT/swapped"
    test "$(inode "$first")" = "$(inode "$anchor")"
    if LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_SWAP_MARKER="$TEST_ROOT/swapped" \
        NIX_TEST_OPTIMISE_READ_GUARD="$anchor" \
        nix-hash --type sha256 --base32 "$anchor" > "$TEST_ROOT/read-guard.log" 2>&1; then
        fail "inode-cache read guard was not exercised"
    fi
    nix-store --verify-path "${paths[@]}"
    assertClean

    detachPlain() {
        path="${paths[0]}"
        chmod u+w "$path"
        cp --reflink=never "$path/plain" "$path/replacement"
        mv "$path/replacement" "$path/plain"
        chmod a-w "$path"
    }

    waitReady() {
        for ((attempt = 0; attempt < 1000; attempt++)); do
            if [[ -s "$1.ready" ]]; then
                return
            fi
            sleep 0.005
        done
        fail "optimisation did not reach the controlled pause"
    }

    detachPlain
    LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_PAUSE="$TEST_ROOT/gc-pause" \
        nix-store --optimise --option optimise-store-jobs 4 > "$TEST_ROOT/optimise-gc.log" 2>&1 &
    optimisePid=$!
    waitReady "$TEST_ROOT/gc-pause"
    tempLink=$(cat "$TEST_ROOT/gc-pause.ready")
    test -f "$tempLink"
    test "$(stat --format=%a "$(dirname "$tempLink")")" = 700
    garbage="$NIX_STORE_DIR/77777777777777777777777777777777-garbage"
    printf 'garbage' > "$garbage"
    printf '%s\n\n0\n' "$garbage" | nix-store --register-validity
    nix-store --gc > "$TEST_ROOT/gc.log" 2>&1
    test ! -e "$garbage"
    test -f "$tempLink"
    touch "$TEST_ROOT/gc-pause.release"
    wait "$optimisePid"
    nix-store --verify-path "${paths[@]}"
    assertClean

    detachPlain
    LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_PAUSE="$TEST_ROOT/int-pause" \
        nix-store --optimise --option optimise-store-jobs 4 > "$TEST_ROOT/interrupt.log" 2>&1 &
    optimisePid=$!
    waitReady "$TEST_ROOT/int-pause"
    kill -INT "$optimisePid"
    waitReady "$TEST_ROOT/int-pause.signal"
    touch "$TEST_ROOT/int-pause.release"
    if wait "$optimisePid"; then
        fail "interrupted optimisation unexpectedly succeeded"
    fi
    grepQuiet 'interrupted' "$TEST_ROOT/interrupt.log"
    assertClean
    nix-store --verify-path "${paths[@]}"

    for path in "${paths[@]:0:32}"; do
        chmod u+w "$path"
        cp --reflink=never "$path/plain" "$path/replacement"
        mv "$path/replacement" "$path/plain"
        chmod a-w "$path"
    done
    lateInode=$(inode "${paths[31]}/plain")
    LD_PRELOAD="$OPTIMISE_TEST_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}" \
        NIX_TEST_OPTIMISE_PAUSE="$TEST_ROOT/cancel-pause" \
        NIX_TEST_OPTIMISE_CANCEL="$TEST_ROOT/cancel-pause" \
        nix-store --optimise --option optimise-store-jobs 2 > "$TEST_ROOT/cancel.log" 2>&1 &
    optimisePid=$!
    waitReady "$TEST_ROOT/cancel-pause"
    waitReady "$TEST_ROOT/cancel-pause.error"
    errorTid=$(cat "$TEST_ROOT/cancel-pause.error.ready")
    for ((attempt = 0; attempt < 1000; attempt++)); do
        if [[ ! -d "/proc/$optimisePid/task/$errorTid" ]]; then
            break
        fi
        sleep 0.005
    done
    test ! -d "/proc/$optimisePid/task/$errorTid"
    touch "$TEST_ROOT/cancel-pause.release"
    if wait "$optimisePid"; then
        fail "background worker error unexpectedly succeeded"
    fi
    test "$(inode "${paths[31]}/plain")" = "$lateInode"
    grepQuiet 'renaming' "$TEST_ROOT/cancel.log"
    assertClean
    nix-store --verify-path "${paths[@]}"
fi

nix-store --optimise --option optimise-store-jobs 4
nix-store --verify --check-contents
assertClean
