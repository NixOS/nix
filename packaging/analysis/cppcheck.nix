{
  pkgs,
  mkCompileDbReport,
}:

let
  runner = pkgs.writeShellApplication {
    name = "run-cppcheck-analysis";
    runtimeInputs = with pkgs; [
      cppcheck
      coreutils
      gnugrep
    ];
    text = ''
      compile_db="$1"
      # shellcheck disable=SC2034
      source_dir="$2"
      output_dir="$3"

      echo "=== cppcheck Analysis ==="

      # Use --project for compilation database (cannot combine with source args)
      cppcheck \
        --project="$compile_db/compile_commands.json" \
        --enable=all \
        --std=c++20 \
        --suppress=missingInclude \
        --suppress=unusedFunction \
        --suppress=unmatchedSuppression \
        --xml \
        2> "$output_dir/report.xml" || true

      # Also produce a human-readable text report
      cppcheck \
        --project="$compile_db/compile_commands.json" \
        --enable=all \
        --std=c++20 \
        --suppress=missingInclude \
        --suppress=unusedFunction \
        --suppress=unmatchedSuppression \
        2> "$output_dir/report.txt" || true

      findings=$(grep -c '\(error\|warning\|style\|performance\|portability\)' "$output_dir/report.txt" || echo "0")
      echo "$findings" > "$output_dir/count.txt"
    '';
  };
in
mkCompileDbReport "cppcheck" runner
