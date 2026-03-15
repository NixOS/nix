{
  pkgs,
  mkCompileDbReport,
  nixTidyChecks ? null,
}:

let
  loadFlag = if nixTidyChecks != null then "--load=${nixTidyChecks}/lib/libnix-tidy.so" else "";

  runner = pkgs.writeShellApplication {
    name = "run-clang-tidy-analysis";
    runtimeInputs = with pkgs; [
      clang-tools
      coreutils
      findutils
      gnugrep
    ];
    text = ''
      compile_db="$1"
      source_dir="$2"
      output_dir="$3"

      echo "=== clang-tidy Analysis ==="
      echo "Using compilation database: $compile_db"

      # Find all .cc source files in library directories
      find "$source_dir/src" -name '*.cc' -not -path '*/test*' -print0 | \
        xargs -0 -P "$(nproc)" -I{} \
          clang-tidy ${loadFlag} -p "$compile_db" --header-filter='src/.*' {} \
        > "$output_dir/report.txt" 2>&1 || true

      findings=$(grep -c ': warning:\|: error:' "$output_dir/report.txt" || echo "0")
      echo "$findings" > "$output_dir/count.txt"
    '';
  };
in
mkCompileDbReport "clang-tidy" runner
