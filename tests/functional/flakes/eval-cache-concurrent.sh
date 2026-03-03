#!/usr/bin/env bash

# Test concurrent access to the eval cache by multiple processes.
# This verifies that the SQLite process-safety changes work correctly
# when multiple nix processes access the same eval cache simultaneously.

source ./common.sh

requireGit

# Configuration
NUM_PROCESSES=${NIX_TEST_CONCURRENT_PROCESSES:-5}
NUM_ITERATIONS=${NIX_TEST_CONCURRENT_ITERATIONS:-3}

flakeDir="$TEST_ROOT/eval-cache-concurrent-flake"

# Create a flake with multiple outputs that can be built in parallel
create_test_flake() {
    createGitRepo "$flakeDir" ""
    cp ../simple.nix ../simple.builder.sh "${config_nix}" "$flakeDir/"
    git -C "$flakeDir" add simple.nix simple.builder.sh config.nix

    cat >"$flakeDir/flake.nix" <<'FLAKE_EOF'
{
  description = "Concurrent eval-cache test flake";
  outputs = { self }: let
    inherit (import ./config.nix) mkDerivation;
    makeOutput = name: mkDerivation {
      name = name;
      buildCommand = ''
        echo "Building ${name}" > $out
      '';
    };
  in {
    # Create multiple outputs for parallel access
    output1 = makeOutput "output1";
    output2 = makeOutput "output2";
    output3 = makeOutput "output3";
    output4 = makeOutput "output4";
    output5 = makeOutput "output5";

    # Nested outputs to test deeper cache paths
    nested = {
      deep1 = makeOutput "deep1";
      deep2 = makeOutput "deep2";
      deep3 = makeOutput "deep3";
    };

    # Attrset with string values (to test string caching)
    metadata = {
      version = "1.0.0";
      description = "Test package";
      homepage = "https://example.com";
      maintainers = ["alice" "bob" "charlie"];
    };
  };
}
FLAKE_EOF

    git -C "$flakeDir" add flake.nix
    git -C "$flakeDir" commit -m "Init"
}

# Run multiple nix eval commands in parallel
test_parallel_eval() {
    echo "Testing parallel eval access..."

    local pids=()
    local results_dir="$TEST_ROOT/results"
    mkdir -p "$results_dir"

    for i in $(seq 1 "$NUM_PROCESSES"); do
        (
            # Each process evaluates different attributes
            nix eval "$flakeDir#output$((i % 5 + 1))" --json > "$results_dir/eval_$i.out" 2>&1
            echo $? > "$results_dir/eval_$i.exit"
        ) &
        pids+=($!)
    done

    # Wait for all processes
    local failed=0
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            failed=$((failed + 1))
        fi
    done

    # Check results
    for i in $(seq 1 "$NUM_PROCESSES"); do
        if [[ ! -f "$results_dir/eval_$i.exit" ]] || [[ "$(cat "$results_dir/eval_$i.exit")" != "0" ]]; then
            echo "Process $i failed:"
            cat "$results_dir/eval_$i.out" || true
            failed=$((failed + 1))
        fi
    done

    if [[ $failed -gt 0 ]]; then
        echo "FAILED: $failed processes failed"
        return 1
    fi

    echo "PASSED: All parallel eval processes succeeded"
}

# Run multiple nix build commands in parallel
test_parallel_build() {
    echo "Testing parallel build access..."

    local pids=()
    local results_dir="$TEST_ROOT/build_results"
    mkdir -p "$results_dir"

    for i in $(seq 1 "$NUM_PROCESSES"); do
        (
            nix build "$flakeDir#output$((i % 5 + 1))" --no-link > "$results_dir/build_$i.out" 2>&1
            echo $? > "$results_dir/build_$i.exit"
        ) &
        pids+=($!)
    done

    # Wait for all processes
    local failed=0
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            failed=$((failed + 1))
        fi
    done

    # Check results
    for i in $(seq 1 "$NUM_PROCESSES"); do
        if [[ ! -f "$results_dir/build_$i.exit" ]] || [[ "$(cat "$results_dir/build_$i.exit")" != "0" ]]; then
            echo "Process $i failed:"
            cat "$results_dir/build_$i.out" || true
            failed=$((failed + 1))
        fi
    done

    if [[ $failed -gt 0 ]]; then
        echo "FAILED: $failed build processes failed"
        return 1
    fi

    echo "PASSED: All parallel build processes succeeded"
}

# Test concurrent reads and writes to the cache
test_concurrent_read_write() {
    echo "Testing concurrent read/write access..."

    local pids=()
    local results_dir="$TEST_ROOT/rw_results"
    mkdir -p "$results_dir"

    # Start writers (build commands that populate cache)
    for i in $(seq 1 3); do
        (
            nix build "$flakeDir#nested.deep$i" --no-link > "$results_dir/write_$i.out" 2>&1
            echo $? > "$results_dir/write_$i.exit"
        ) &
        pids+=($!)
    done

    # Start readers (eval commands that read from cache)
    for i in $(seq 1 3); do
        (
            nix eval "$flakeDir#metadata.version" --json > "$results_dir/read_$i.out" 2>&1
            echo $? > "$results_dir/read_$i.exit"
        ) &
        pids+=($!)
    done

    # Wait for all processes
    local failed=0
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            failed=$((failed + 1))
        fi
    done

    # Check write results
    for i in $(seq 1 3); do
        if [[ ! -f "$results_dir/write_$i.exit" ]] || [[ "$(cat "$results_dir/write_$i.exit")" != "0" ]]; then
            echo "Writer $i failed:"
            cat "$results_dir/write_$i.out" || true
            failed=$((failed + 1))
        fi
    done

    # Check read results
    for i in $(seq 1 3); do
        if [[ ! -f "$results_dir/read_$i.exit" ]] || [[ "$(cat "$results_dir/read_$i.exit")" != "0" ]]; then
            echo "Reader $i failed:"
            cat "$results_dir/read_$i.out" || true
            failed=$((failed + 1))
        fi
    done

    if [[ $failed -gt 0 ]]; then
        echo "FAILED: $failed read/write processes failed"
        return 1
    fi

    echo "PASSED: Concurrent read/write access succeeded"
}

# Run multiple iterations to increase chance of race condition detection
test_stress() {
    echo "Running stress test with $NUM_ITERATIONS iterations..."

    for iter in $(seq 1 "$NUM_ITERATIONS"); do
        echo "--- Iteration $iter ---"

        # Clear eval cache between iterations to ensure fresh state
        rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/nix/eval-cache-v6/"*.sqlite 2>/dev/null || true

        test_parallel_eval
        test_parallel_build
        test_concurrent_read_write
    done

    echo "Stress test completed successfully!"
}

# Main test execution
create_test_flake
test_stress
