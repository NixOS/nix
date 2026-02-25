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
  unitTests = {
    "nix-util-tests" = fixOutput packages."nix-util-tests-x86_64-w64-mingw32".passthru.tests.run;
  };
}
