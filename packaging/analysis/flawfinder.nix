{
  pkgs,
  mkSourceReport,
}:

let
  runner = pkgs.writeShellApplication {
    name = "run-flawfinder-analysis";
    runtimeInputs = with pkgs; [
      flawfinder
      coreutils
      gnugrep
    ];
    text = ''
      source_dir="$1"
      output_dir="$2"

      echo "=== flawfinder Analysis ==="

      flawfinder \
        --minlevel=1 \
        --columns \
        --context \
        --singleline \
        "$source_dir/src" \
        > "$output_dir/report.txt" 2>&1 || true

      # Extract hit count from flawfinder's summary line: "Hits = N"
      findings=$(grep -oP 'Hits = \K[0-9]+' "$output_dir/report.txt" || echo "0")
      echo "$findings" > "$output_dir/count.txt"
    '';
  };
in
mkSourceReport "flawfinder" runner
