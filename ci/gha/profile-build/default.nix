{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  pkgs ? nixFlake.inputs.nixpkgs.legacyPackages.${system},
}:

let
  inherit (pkgs) lib;

  nixComponentsInstrumented =
    (nixFlake.lib.makeComponents {
      inherit pkgs;
      getStdenv = p: p.clangStdenv;
    }).overrideScope
      (
        _: _: {
          mesonComponentOverrides = finalAttrs: prevAttrs: {
            outputs = (prevAttrs.outputs or [ "out" ]) ++ [ "buildprofile" ];
            nativeBuildInputs = [ pkgs.clangbuildanalyzer ] ++ prevAttrs.nativeBuildInputs or [ ];
            __impure = true;

            env = {
              CFLAGS = "-ftime-trace";
              CXXFLAGS = "-ftime-trace";
            };

            preBuild = ''
              ClangBuildAnalyzer --start $PWD
            '';

            postBuild = ''
              ClangBuildAnalyzer --stop $PWD $buildprofile
            '';
          };
        }
      );

  componentsToProfile = {
    "nix-util" = { };
    "nix-util-c" = { };
    "nix-util-test-support" = { };
    "nix-util-tests" = { };
    "nix-store" = { };
    "nix-store-c" = { };
    "nix-store-test-support" = { };
    "nix-store-tests" = { };
    "nix-fetchers" = { };
    "nix-fetchers-c" = { };
    "nix-fetchers-tests" = { };
    "nix-expr" = { };
    "nix-expr-c" = { };
    "nix-expr-test-support" = { };
    "nix-expr-tests" = { };
    "nix-flake" = { };
    "nix-flake-c" = { };
    "nix-flake-tests" = { };
    "nix-main" = { };
    "nix-main-c" = { };
    "nix-cmd" = { };
    "nix-cli" = { };
  };

  componentDerivationsToProfile = builtins.intersectAttrs componentsToProfile nixComponentsInstrumented;
  componentBuildProfiles = lib.mapAttrs (
    n: v: lib.getOutput "buildprofile" v
  ) componentDerivationsToProfile;

  buildTimeReport =
    pkgs.runCommand "build-time-report"
      {
        __impure = true;
        __structuredAttrs = true;
        nativeBuildInputs = [ pkgs.clangbuildanalyzer ];
        inherit componentBuildProfiles;
      }
      ''
        {
          echo "# Build time performance profile for components:"
          echo
          echo "This reports the build profile collected via \`-ftime-trace\` for each component."
          echo
        } >> $out

        for name in "''\${!componentBuildProfiles[@]}"; do
          {
            echo "<details><summary><strong>$name</strong></summary>"
            echo
            echo '````'
            ClangBuildAnalyzer --analyze "''\${componentBuildProfiles[$name]}"
            echo '````'
            echo
            echo "</details>"
          } >> $out
        done
      '';
in

{
  inherit buildTimeReport;
  inherit componentDerivationsToProfile;
}
