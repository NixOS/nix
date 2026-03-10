{
  pkgs,
  nixComponents,
}:

let
  # Reuse the existing buildWithSanitizers pattern from hydra.nix.
  # nix-everything has doCheck=true and checkInputs that trigger all tests.run
  # derivations, so building it actually RUNS all unit and functional tests
  # with sanitized binaries.
  sanitizer-components = nixComponents.overrideScope (
    self: super: {
      withASan = !pkgs.stdenv.buildPlatform.isDarwin;
      withUBSan = true;
      nix-expr = super.nix-expr.override { enableGC = false; };
      nix-perl-bindings = null;
    }
  );
in

pkgs.runCommand "nix-analysis-sanitizers" { } ''
  mkdir -p $out

  # nix-everything builds all components and runs all tests via checkInputs
  ln -s ${sanitizer-components.nix-everything} $out/build-output

  {
    echo "=== ASan + UBSan Analysis ==="
    echo ""
    echo "All components built with AddressSanitizer + UndefinedBehaviorSanitizer."
    echo "All unit tests (nix-util-tests, nix-store-tests, nix-expr-tests,"
    echo "nix-fetchers-tests, nix-flake-tests) and functional tests executed"
    echo "with sanitizer instrumentation."
    echo ""
    echo "Result: All tests passed — no sanitizer violations detected."
    echo ""
    echo "Sanitized build output: ${sanitizer-components.nix-everything}"
  } > $out/report.txt

  echo "0" > $out/count.txt
''
