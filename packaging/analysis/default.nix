{
  lib,
  pkgs,
  src,
  nixComponents,
}:

let
  inherit (pkgs.buildPackages)
    meson
    ninja
    pkg-config
    bison
    flex
    cmake
    ;

  # ── Compilation database ────────────────────────────────────────

  compilationDb = import ./compile-db.nix {
    inherit
      lib
      pkgs
      src
      nixComponents
      ;
  };

  # ── Shared build configuration ─────────────────────────────────
  #
  # gcc-warnings and gcc-analyzer build from source in standalone derivations
  # so we can capture ninja output directly (component build logs are not
  # accessible from downstream derivations).

  mesonConfigureArgs = lib.concatStringsSep " " [
    "--prefix=$out"
    "-Dunit-tests=false"
    "-Djson-schema-checks=false"
    "-Dbindings=false"
    "-Ddoc-gen=false"
    "-Dbenchmarks=false"
  ];

  analysisNativeBuildInputs = [
    meson
    ninja
    pkg-config
    bison
    flex
    cmake
    pkgs.buildPackages.python3
  ];

  analysisBuildInputs = compilationDb.buildInputs;

  # ── Helper for tools that need compilation database ─────────────

  mkCompileDbReport =
    name: script:
    pkgs.runCommand "nix-analysis-${name}"
      {
        nativeBuildInputs = [ script ];
      }
      ''
        mkdir -p $out
        ${lib.getExe script} ${compilationDb} ${src} $out
      '';

  # ── Helper for tools that work on raw source ────────────────────

  mkSourceReport =
    name: script:
    pkgs.runCommand "nix-analysis-${name}"
      {
        nativeBuildInputs = [ script ];
      }
      ''
        mkdir -p $out
        ${lib.getExe script} ${src} $out
      '';

  # ── Custom clang-tidy plugin ─────────────────────────────────

  nixTidyChecks = import ./clang-tidy-checks.nix {
    inherit pkgs src;
  };

  # ── Individual tool targets ────────────────────────────────────

  clang-tidy = import ./clang-tidy.nix {
    inherit pkgs mkCompileDbReport nixTidyChecks;
  };

  cppcheck = import ./cppcheck.nix {
    inherit pkgs mkCompileDbReport;
  };

  flawfinder = import ./flawfinder.nix {
    inherit pkgs mkSourceReport;
  };

  semgrep = import ./semgrep.nix {
    inherit pkgs mkSourceReport;
  };

  gccTargets = import ./gcc.nix {
    inherit
      lib
      pkgs
      src
      nixComponents
      mesonConfigureArgs
      analysisNativeBuildInputs
      analysisBuildInputs
      ;
  };

  clang-analyzer = import ./clang-analyzer.nix {
    inherit
      lib
      pkgs
      src
      nixComponents
      mesonConfigureArgs
      analysisNativeBuildInputs
      analysisBuildInputs
      ;
  };

  sanitizers = import ./sanitizers.nix {
    inherit pkgs nixComponents;
  };

  # ── Combined targets ───────────────────────────────────────────

  quick = pkgs.runCommand "nix-analysis-quick" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    mkdir -p $out
    ln -s ${clang-tidy} $out/clang-tidy
    ln -s ${cppcheck} $out/cppcheck
    python3 ${src}/packaging/analysis/triage $out --source-root ${src} --output-dir $out/triage
    {
      echo "=== Analysis Summary (quick) ==="
      echo ""
      echo "clang-tidy:      $(cat ${clang-tidy}/count.txt) findings"
      echo "cppcheck:        $(cat ${cppcheck}/count.txt) findings"
      echo "triage:          $(cat $out/triage/count.txt) high-confidence findings"
      echo ""
      echo "Run 'nix build .#analysis-standard' for more thorough analysis."
    } > $out/summary.txt
    cat $out/summary.txt
  '';

  standard = pkgs.runCommand "nix-analysis-standard" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    mkdir -p $out
    ln -s ${clang-tidy} $out/clang-tidy
    ln -s ${cppcheck} $out/cppcheck
    ln -s ${flawfinder} $out/flawfinder
    ln -s ${clang-analyzer} $out/clang-analyzer
    ln -s ${gccTargets.gcc-warnings} $out/gcc-warnings
    python3 ${src}/packaging/analysis/triage $out --source-root ${src} --output-dir $out/triage
    {
      echo "=== Analysis Summary (standard) ==="
      echo ""
      echo "clang-tidy:      $(cat ${clang-tidy}/count.txt) findings"
      echo "cppcheck:        $(cat ${cppcheck}/count.txt) findings"
      echo "flawfinder:      $(cat ${flawfinder}/count.txt) findings"
      echo "clang-analyzer:  $(cat ${clang-analyzer}/count.txt) findings"
      echo "gcc-warnings:    $(cat ${gccTargets.gcc-warnings}/count.txt) findings"
      echo "triage:          $(cat $out/triage/count.txt) high-confidence findings"
      echo ""
      echo "Run 'nix build .#analysis-deep' for full analysis including"
      echo "GCC -fanalyzer, semgrep, and sanitizer builds."
    } > $out/summary.txt
    cat $out/summary.txt
  '';

  deep = pkgs.runCommand "nix-analysis-deep" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    mkdir -p $out
    ln -s ${clang-tidy} $out/clang-tidy
    ln -s ${cppcheck} $out/cppcheck
    ln -s ${flawfinder} $out/flawfinder
    ln -s ${clang-analyzer} $out/clang-analyzer
    ln -s ${gccTargets.gcc-warnings} $out/gcc-warnings
    ln -s ${gccTargets.gcc-analyzer} $out/gcc-analyzer
    ln -s ${semgrep} $out/semgrep
    ln -s ${sanitizers} $out/sanitizers
    python3 ${src}/packaging/analysis/triage $out --source-root ${src} --output-dir $out/triage
    {
      echo "=== Analysis Summary (deep) ==="
      echo ""
      echo "clang-tidy:      $(cat ${clang-tidy}/count.txt) findings"
      echo "cppcheck:        $(cat ${cppcheck}/count.txt) findings"
      echo "flawfinder:      $(cat ${flawfinder}/count.txt) findings"
      echo "clang-analyzer:  $(cat ${clang-analyzer}/count.txt) findings"
      echo "gcc-warnings:    $(cat ${gccTargets.gcc-warnings}/count.txt) findings"
      echo "gcc-analyzer:    $(cat ${gccTargets.gcc-analyzer}/count.txt) findings"
      echo "semgrep:         $(cat ${semgrep}/count.txt) findings"
      echo "sanitizers:      $(cat ${sanitizers}/count.txt) findings"
      echo "triage:          $(cat $out/triage/count.txt) high-confidence findings"
      echo ""
      echo "All analysis tools completed."
    } > $out/summary.txt
    cat $out/summary.txt
  '';

in
{
  inherit
    clang-tidy
    cppcheck
    flawfinder
    clang-analyzer
    semgrep
    sanitizers
    quick
    standard
    deep
    ;
  inherit (gccTargets) gcc-warnings gcc-analyzer;
}
