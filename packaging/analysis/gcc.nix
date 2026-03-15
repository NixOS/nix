{
  lib,
  pkgs,
  src,
  nixComponents,
  mesonConfigureArgs,
  analysisNativeBuildInputs,
  analysisBuildInputs,
}:

let
  gccWarningFlags = [
    "-Wall"
    "-Wextra"
    "-Wpedantic"
    "-Wformat=2"
    "-Wformat-security"
    "-Wshadow"
    "-Wcast-qual"
    "-Wcast-align"
    "-Wwrite-strings"
    "-Wpointer-arith"
    "-Wconversion"
    "-Wsign-conversion"
    "-Wduplicated-cond"
    "-Wduplicated-branches"
    "-Wlogical-op"
    "-Wnull-dereference"
    "-Wdouble-promotion"
    "-Wfloat-equal"
    "-Walloca"
    "-Wvla"
    "-Werror=return-type"
    "-Werror=format-security"
  ];

  # Build the full project from source, capturing ninja output to extract warnings.
  mkGccAnalysisBuild =
    name: extraFlags:
    pkgs.stdenv.mkDerivation {
      pname = "nix-analysis-${name}";
      version = nixComponents.version;
      inherit src;

      nativeBuildInputs = analysisNativeBuildInputs;
      buildInputs = analysisBuildInputs;

      env.NIX_CXXFLAGS_COMPILE = lib.concatStringsSep " " extraFlags;

      dontFixup = true;
      doCheck = false;

      configurePhase = ''
        runHook preConfigure
        meson setup build ${mesonConfigureArgs} \
          || echo "WARNING: meson configure had errors"
        runHook postConfigure
      '';

      buildPhase = ''
        runHook preBuild
        # Build and capture all compiler output (ninja buffers subprocess output
        # and only prints it to stdout, so piping captures warnings too)
        ninja -C build -j''${NIX_BUILD_CORES:-1} 2>&1 | tee "$NIX_BUILD_TOP/build-output.log" || true
        runHook postBuild
      '';

      installPhase = ''
        mkdir -p $out
        # Extract warning/error lines from the build output
        grep -E ': warning:|: error:' "$NIX_BUILD_TOP/build-output.log" > $out/report.txt || true
        findings=$(wc -l < $out/report.txt)
        echo "$findings" > $out/count.txt

        # Include full build log for reference
        cp "$NIX_BUILD_TOP/build-output.log" $out/full-build.log

        {
          echo "=== ${name} Analysis ==="
          echo ""
          echo "Flags: ${lib.concatStringsSep " " extraFlags}"
          echo "Findings: $findings warnings/errors"
          if [ "$findings" -gt 0 ]; then
            echo ""
            echo "=== Warnings ==="
            cat $out/report.txt
          fi
        } > $out/summary.txt
      '';
    };
in
{
  gcc-warnings = mkGccAnalysisBuild "gcc-warnings" gccWarningFlags;

  gcc-analyzer = mkGccAnalysisBuild "gcc-analyzer" [
    "-fanalyzer"
    "-fdiagnostics-plain-output"
  ];
}
