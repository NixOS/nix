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
        # The outer build sets `NIX_STORE` to the host's POSIX store directory,
        # and Wine passes it through to the Windows test binary. A store
        # configured as `FilePathType::Native` then validates that value with
        # `std::filesystem::path::is_absolute()`, which is false for a
        # POSIX-rooted path on Windows because it has no root name. Clearing
        # both lets each store type fall back to its own correct default.
        unset NIX_STORE NIX_STORE_DIR
        {
          ${prev.buildCommand}
        } 2>&1 | ansi2txt
      '';
    });
in

{
  unitTests = {
    "nix-util-tests" = fixOutput packages."nix-util-tests-x86_64-w64-mingw32".passthru.tests.run;
    "nix-store-tests" = fixOutput packages."nix-store-tests-x86_64-w64-mingw32".passthru.tests.run;
  };

  crossBuild = packages."nix-everything-x86_64-w64-mingw32";
}
