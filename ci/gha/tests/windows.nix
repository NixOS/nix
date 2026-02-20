{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  pkgs ? nixFlake.inputs.nixpkgs.legacyPackages.${system},
}:

let
  packages = nixFlake.packages.${system};

  fixOutput =
    test:
    test.overrideAttrs (prev: {
      nativeBuildInputs = prev.nativeBuildInputs or [ ] ++ [ pkgs.colorized-logs ];
      env.GTEST_COLOR = "no";
      # Wine's console emulation wraps every character in ANSI cursor
      # hide/show sequences, making logs unreadable in GitHub Actions.
      buildCommand = ''
        set -o pipefail
        {
          ${prev.buildCommand}
        } 2>&1 | ansi2txt
      '';
    });
in

{
  unitTests = builtins.mapAttrs (n: v: fixOutput packages.${v}.passthru.tests.run) {
    "nix-util-tests" = "nix-util-tests-x86_64-w64-mingw32";
    "nix-store-tests" = "nix-store-tests-x86_64-w64-mingw32";
    "nix-fetchers-tests" = "nix-fetchers-tests-x86_64-w64-mingw32";
    "nix-expr-tests" = "nix-expr-tests-x86_64-w64-mingw32";
    "nix-flake-tests" = "nix-flake-tests-x86_64-w64-mingw32";
  };
}
