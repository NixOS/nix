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
  llvmPkgs = pkgs.llvmPackages;
in

pkgs.stdenv.mkDerivation {
  pname = "nix-analysis-clang-analyzer";
  version = nixComponents.version;
  inherit src;

  nativeBuildInputs = analysisNativeBuildInputs ++ [
    pkgs.clang-analyzer
  ];
  buildInputs = analysisBuildInputs;

  dontFixup = true;
  doCheck = false;

  configurePhase = ''
    runHook preConfigure
    # scan-build wraps the compiler to intercept and analyze each TU
    scan-build \
      --use-analyzer=${llvmPkgs.clang}/bin/clang \
      -enable-checker cplusplus.InnerPointer \
      -enable-checker cplusplus.NewDelete \
      -enable-checker cplusplus.NewDeleteLeaks \
      -enable-checker cplusplus.PlacementNew \
      -enable-checker cplusplus.PureVirtualCall \
      -enable-checker alpha.cplusplus.DeleteWithNonVirtualDtor \
      -enable-checker alpha.cplusplus.InvalidatedIterator \
      -enable-checker alpha.cplusplus.IteratorRange \
      -enable-checker alpha.cplusplus.MismatchedIterator \
      -enable-checker alpha.cplusplus.Move \
      meson setup build ${mesonConfigureArgs} \
      || echo "WARNING: meson configure had errors"
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    scan-build \
      --use-analyzer=${llvmPkgs.clang}/bin/clang \
      -enable-checker cplusplus.InnerPointer \
      -enable-checker cplusplus.NewDelete \
      -enable-checker cplusplus.NewDeleteLeaks \
      -enable-checker cplusplus.PlacementNew \
      -enable-checker cplusplus.PureVirtualCall \
      -enable-checker alpha.cplusplus.DeleteWithNonVirtualDtor \
      -enable-checker alpha.cplusplus.InvalidatedIterator \
      -enable-checker alpha.cplusplus.IteratorRange \
      -enable-checker alpha.cplusplus.MismatchedIterator \
      -enable-checker alpha.cplusplus.Move \
      -o "$NIX_BUILD_TOP/scan-results" \
      ninja -C build -j''${NIX_BUILD_CORES:-1} \
      2>&1 | tee "$NIX_BUILD_TOP/scan-build.log" || true
    runHook postBuild
  '';

  installPhase = ''
    mkdir -p $out

    # Copy HTML reports if produced
    if [ -d "$NIX_BUILD_TOP/scan-results" ] && [ "$(ls -A "$NIX_BUILD_TOP/scan-results" 2>/dev/null)" ]; then
      cp -r "$NIX_BUILD_TOP/scan-results"/* $out/html-report/ 2>/dev/null || true
    fi

    # Extract finding count from scan-build output
    findings=$(grep -oP '\d+ bugs? found' "$NIX_BUILD_TOP/scan-build.log" | grep -oP '^\d+' || echo "0")
    echo "$findings" > $out/count.txt

    cp "$NIX_BUILD_TOP/scan-build.log" $out/report.txt

    {
      echo "=== Clang Static Analyzer ==="
      echo ""
      echo "Path-sensitive analysis with C++-specific checkers."
      echo "Findings: $findings"
    } > $out/summary.txt
  '';
}
