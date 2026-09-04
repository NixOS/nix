{
  lib,
  coreutils,
  runCommand,
  stdenv,
}:

{
  package,
  targets,
}:

{
  runs ? 0,
}:

assert lib.assertMsg (builtins.isInt runs) "fuzz seed runs must be an integer";
assert lib.assertMsg (runs >= 0) "fuzz seed runs must not be negative";
assert lib.assertMsg stdenv.hostPlatform.isLinux "fuzz seed checks require a Linux host";
assert lib.assertMsg (stdenv.buildPlatform.canExecute stdenv.hostPlatform)
  "fuzz seed checks require a native build";

runCommand "${package.pname}-fuzz-seeds${lib.optionalString (runs > 0) "-${toString runs}-runs"}"
  { }
  ''
    set -euo pipefail

    export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

    run_target() {
      local target=$1
      local corpus=$2
      local dictionary=$3
      local seeds=("$corpus"/*)
      local args=(
        -timeout=5
        -rss_limit_mb=1024
        -malloc_limit_mb=1024
      )

      if (( ''${#seeds[@]} == 0 )); then
        echo "fuzz corpus is empty: $corpus" >&2
        return 1
      fi

      if [[ -n "$dictionary" ]]; then
        args+=("-dict=$dictionary")
      fi

      ${lib.getExe' coreutils "timeout"} --kill-after=10s 90s \
        "${package}/bin/$target${stdenv.hostPlatform.extensions.executable}" \
        -runs=1 \
        "''${args[@]}" \
        "''${seeds[@]}"

      if (( ${toString runs} > 0 )); then
        local writable_corpus="$TMPDIR/$target-corpus"
        local total_runs=$(( ''${#seeds[@]} + 1 + ${toString runs} ))
        mkdir "$writable_corpus"

        "${package}/bin/$target${stdenv.hostPlatform.extensions.executable}" \
          "-runs=$total_runs" \
          -detect_leaks=0 \
          "''${args[@]}" \
          "$writable_corpus" \
          "$corpus"
      fi
    }

    shopt -s nullglob

    ${lib.concatMapStringsSep "\n" (
      {
        name,
        corpus,
        dictionary ? null,
      }:
      "run_target ${lib.escapeShellArg name} ${corpus}"
      + (if dictionary == null then " ''" else " ${dictionary}")
    ) targets}

    touch $out
  ''
