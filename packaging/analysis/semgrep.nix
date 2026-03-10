{
  pkgs,
  mkSourceReport,
}:

let
  rulesFile = ./semgrep-rules.yaml;

  runner = pkgs.writeShellApplication {
    name = "run-semgrep-analysis";
    runtimeInputs = with pkgs; [
      semgrep
      coreutils
      gnugrep
      cacert
    ];
    text = ''
      source_dir="$1"
      output_dir="$2"

      echo "=== semgrep Analysis ==="

      export SEMGREP_ENABLE_VERSION_CHECK=0
      export SEMGREP_SEND_METRICS=off
      export SSL_CERT_FILE="${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
      export OTEL_TRACES_EXPORTER=none
      # semgrep needs a writable HOME for its config/cache
      HOME="$(mktemp -d)"
      export HOME

      semgrep \
        --config ${rulesFile} \
        --json \
        --metrics=off \
        --no-git-ignore \
        "$source_dir/src" \
        > "$output_dir/report.json" 2>&1 || true

      # Also produce a text report
      semgrep \
        --config ${rulesFile} \
        --metrics=off \
        --no-git-ignore \
        "$source_dir/src" \
        > "$output_dir/report.txt" 2>&1 || true

      # Count results from JSON output
      findings=$(grep -o '"check_id"' "$output_dir/report.json" | wc -l || echo "0")
      echo "$findings" > "$output_dir/count.txt"
    '';
  };
in
mkSourceReport "semgrep" runner
