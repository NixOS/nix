{
  lib,
  stdenv,
  mkMesonExecutable,
  writeShellScript,
  runCommand,
  makeWrapper,

  # These will be AFL++-instrumented versions when built properly
  nix-util,
  nix-store,
  nix-fetchers,
  nix-expr,

  # AFL++ is required for fuzzing
  aflplusplus,

  # Configuration Options

  version,
  resolvePath,
}:

let
  inherit (lib) fileset;

  # Build the actual fuzzer binary (harness)
  fuzzerHarness = mkMesonExecutable (finalAttrs: {
    pname = "nix-expr-fuzzer";
    inherit version;

    workDir = ./.;
    fileset = fileset.unions [
      ../../nix-meson-build-support
      ./nix-meson-build-support
      ../../.version
      ./.version
      ./meson.build
      (fileset.fileFilter (file: file.hasExt "cc") ./.)
      (fileset.fileFilter (file: file.hasExt "hh") ./.)
      ./nix.dict
    ];

    buildInputs = [
      nix-util
      nix-store
      nix-fetchers
      nix-expr
      aflplusplus
    ];

    # Use AFL++ LTO mode instrumentation (recommended by AFL++ docs)
    preConfigure = ''
      echo "Configuring AFL++ LTO instrumentation with sanitizers..."
      export CC="${aflplusplus}/bin/afl-clang-lto"
      export CXX="${aflplusplus}/bin/afl-clang-lto++"
      export AFL_QUIET=1
      export AFL_USE_ASAN=1
      export AFL_USE_UBSAN=1
    '';

    mesonFlags = [
      (lib.mesonOption "b_sanitize" "address,undefined")
      (lib.mesonBool "b_lundef" false) # Required for Clang with sanitizers
    ];

    meta = {
      description = "AFL++ fuzzing harness binary for the Nix expression evaluator";
      platforms = lib.platforms.unix;
      mainProgram = "nix-expr-fuzzer";
    };
  });

  # Corpus location
  corpus = resolvePath ./corpus;

  # Script to run parallel fuzzing
  parallelScript = writeShellScript "nix-expr-fuzz-parallel" ''
    set -euo pipefail

    # Parse arguments
    RESUME=false
    ARGS=()
    for arg in "$@"; do
      if [ "$arg" = "--resume" ]; then
        RESUME=true
      else
        ARGS+=("$arg")
      fi
    done

    NUM_FUZZERS="''${ARGS[0]:-$(nproc)}"
    OUTPUT_DIR="''${ARGS[1]:-$HOME/tmp/nix-fuzz/fuzz-findings}"

    if [ "$NUM_FUZZERS" -lt 1 ]; then
      echo "Error: Need at least 1 fuzzer"
      echo "Usage: $0 [--resume] [num_fuzzers] [output_dir]"
      echo ""
      echo "Examples:"
      echo "  $0                   # Use all CPU cores, fresh start"
      echo "  $0 --resume          # Resume previous session with all cores"
      echo "  $0 32                # Run 32 parallel fuzzers, fresh start"
      echo "  $0 --resume 32       # Resume with 32 cores"
      echo "  $0 8 ./findings      # Run 8 fuzzers, output to ./findings"
      echo "  $0 --resume 8 ./out  # Resume with 8 cores to ./out"
      exit 1
    fi

    # Set input corpus based on resume flag
    if [ "$RESUME" = true ]; then
      INPUT_ARG="-"
      echo "Resuming previous fuzzing session..."
    else
      INPUT_ARG="${corpus}"
      echo "Starting new fuzzing session..."
    fi

    echo "Starting $NUM_FUZZERS parallel AFL++ fuzzers..."
    echo "Output directory: $OUTPUT_DIR"
    echo ""

    mkdir -p "$OUTPUT_DIR"

    # Cleanup function to kill all fuzzers on exit
    cleanup() {
      echo ""
      echo "Stopping all fuzzer instances..."
      jobs -p | xargs -r kill 2>/dev/null || true
      wait
      echo "All fuzzers stopped."
    }
    trap cleanup EXIT INT TERM

    # Create log directory
    LOG_DIR="$OUTPUT_DIR/logs"
    mkdir -p "$LOG_DIR"

    # Start main fuzzer in background (with dictionary)
    echo "Starting main fuzzer (fuzzer-main) with Nix dictionary..."
    ${aflplusplus}/bin/afl-fuzz \
      -M fuzzer-main \
      -i "$INPUT_ARG" \
      -o "$OUTPUT_DIR" \
      -x ${resolvePath ./nix.dict} \
      -- ${fuzzerHarness}/bin/nix-expr-fuzzer \
      > "$LOG_DIR/fuzzer-main.log" 2>&1 &
    MAIN_PID=$!

    # Wait for main to initialize
    sleep 2

    # Start secondary fuzzers (also with dictionary)
    for i in $(seq 2 "$NUM_FUZZERS"); do
      FUZZER_NAME="fuzzer-$i"
      echo "Starting secondary fuzzer ($FUZZER_NAME)..."
      ${aflplusplus}/bin/afl-fuzz \
        -S "$FUZZER_NAME" \
        -i "$INPUT_ARG" \
        -o "$OUTPUT_DIR" \
        -x ${resolvePath ./nix.dict} \
        -- ${fuzzerHarness}/bin/nix-expr-fuzzer \
        > "$LOG_DIR/$FUZZER_NAME.log" 2>&1 &
      sleep 0.5
    done

    echo ""
    echo "✓ All $NUM_FUZZERS fuzzers started!"
    echo ""
    echo "Fuzzer PIDs: $(jobs -p | tr '\n' ' ')"
    echo "Output directory: $OUTPUT_DIR"
    echo "Logs: $LOG_DIR/"
    echo ""
    echo "Starting continuous status monitoring..."
    echo "Press Ctrl+C to stop all fuzzers"
    echo ""

    # Wait a bit for fuzzers to initialize
    sleep 3

    echo "Logs: $LOG_DIR/"
    echo "Press Ctrl+C to stop all fuzzers"

    # Continuously run afl-whatsup to show status
    watch -tn0 ${aflplusplus}/bin/afl-whatsup -s "$OUTPUT_DIR" || true

    wait
  '';

  # Script to reproduce crashes
  reproduceScript = writeShellScript "nix-expr-fuzz-reproduce" ''
    set -euo pipefail

    if [ $# -eq 0 ]; then
      echo "Usage: $0 <crash_file>"
      echo ""
      echo "Reproduce a crash found by AFL++ with full sanitizer output."
      echo ""
      echo "Example:"
      echo "  $0 fuzz-findings/default/crashes/id:000000,sig:06,..."
      exit 1
    fi

    CRASH_FILE="$1"

    if [ ! -f "$CRASH_FILE" ]; then
      echo "Error: Crash file not found: $CRASH_FILE"
      exit 1
    fi

    echo "=== Reproducing Crash ==="
    echo "Crash file: $CRASH_FILE"
    echo ""
    echo "File contents:"
    cat "$CRASH_FILE"
    echo ""
    echo "=== Running with full sanitizer output ==="

    # Enable full symbolization and leak detection for crash reproduction
    export ASAN_OPTIONS="abort_on_error=1:symbolize=1:detect_leaks=1:print_summary=1"
    export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:symbolize=1"

    exec ${fuzzerHarness}/bin/nix-expr-fuzzer "$CRASH_FILE"
  '';

in

# Create wrapper package that runs AFL++ with the fuzzer
runCommand "nix-expr-fuzz-${version}"
  {
    nativeBuildInputs = [ makeWrapper ];

    passthru = {
      # Expose the raw fuzzer binary
      harness = fuzzerHarness;
      # Expose corpus location
      inherit corpus;
      # Expose AFL++ for convenience
      inherit aflplusplus;
    };

    meta = {
      description = "AFL++ fuzzer for the Nix expression evaluator";
      longDescription = ''
        Complete AFL++ fuzzing setup for the Nix evaluator.
        Run this package to start fuzzing with optimal configuration.

        The fuzzer is built with:
        - AFL++ compile-time instrumentation (4000+ exec/sec)
        - Address Sanitizer (ASAN) for memory corruption detection
        - Undefined Behavior Sanitizer (UBSAN) for UB detection
      '';
      platforms = lib.platforms.unix;
      mainProgram = "nix-expr-fuzz";
    };
  }
  ''
    mkdir -p $out/bin $out/share/nix-expr-fuzz

    # Install corpus and dictionary
    cp -r ${corpus} $out/share/nix-expr-fuzz/corpus
    cp ${resolvePath ./nix.dict} $out/share/nix-expr-fuzz/nix.dict

    # Install helper scripts
    ln -s ${parallelScript} $out/bin/nix-expr-fuzz-parallel
    ln -s ${reproduceScript} $out/bin/nix-expr-fuzz-reproduce

    # Create wrapper script that runs AFL++ (single instance)
    # Default output to tmpfs for better performance
    makeWrapper ${aflplusplus}/bin/afl-fuzz $out/bin/nix-expr-fuzz \
      --add-flags "-i $out/share/nix-expr-fuzz/corpus" \
      --add-flags "-o" \
      --add-flags "\''${FUZZ_OUTPUT_DIR:-\$HOME/tmp/nix-fuzz/fuzz-findings}" \
      --add-flags "-x $out/share/nix-expr-fuzz/nix.dict" \
      --add-flags "--" \
      --add-flags "${fuzzerHarness}/bin/nix-expr-fuzzer" \
      --set AFL_SKIP_CPUFREQ 1 \
      --set AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES 1 \
      --set AFL_AUTORESUME 1

    # Also provide direct access to the fuzzer binary
    ln -s ${fuzzerHarness}/bin/nix-expr-fuzzer $out/bin/nix-expr-fuzz-harness
  ''
