{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  pkgs ? nixFlake.inputs.nixpkgs.legacyPackages.${system},
  getStdenv ? p: p.stdenv,
  componentTestsPrefix ? "",
  withSanitizers ? false,
}:

let
  inherit (pkgs) lib;
  hydraJobs = nixFlake.hydraJobs;
  packages' = nixFlake.packages.${system};
in

{
  /**
    Top-level tests for the flake outputs, as they would be built by hydra.
    These tests generally can't be overridden to run with sanitizers.
  */
  topLevel = {
    installerScriptForGHA = hydraJobs.installerScriptForGHA.${system};
    installTests = hydraJobs.installTests.${system};
    nixpkgsLibTests = hydraJobs.tests.nixpkgsLibTests.${system};
    rl-next = pkgs.buildPackages.runCommand "test-rl-next-release-notes" { } ''
      LANG=C.UTF-8 ${pkgs.changelog-d}/bin/changelog-d ${../../../doc/manual/rl-next} >$out
    '';
    repl-completion = pkgs.callPackage ../../../tests/repl-completion.nix { inherit (packages') nix; };

    /**
      Checks for our packaging expressions.
      This shouldn't build anything significant; just check that things
      (including derivations) are _set up_ correctly.
    */
    packaging-overriding =
      let
        nix = packages'.nix;
      in
      assert (nix.appendPatches [ pkgs.emptyFile ]).libs.nix-util.src.patches == [ pkgs.emptyFile ];
      if pkgs.stdenv.buildPlatform.isDarwin then
        lib.warn "packaging-overriding check currently disabled because of a permissions issue on macOS" pkgs.emptyFile
      else
        # If this fails, something might be wrong with how we've wired the scope,
        # or something could be broken in Nixpkgs.
        pkgs.testers.testEqualContents {
          assertion = "trivial patch does not change source contents";
          expected = "${../../..}";
          actual =
            # Same for all components; nix-util is an arbitrary pick
            (nix.appendPatches [ pkgs.emptyFile ]).libs.nix-util.src;
        };
  };

  componentTests =
    let
      nixComponents =
        (nixFlake.lib.makeComponents {
          inherit pkgs;
          inherit getStdenv;
        }).overrideScope
          (
            final: prev: {
              nix-store-tests = prev.nix-store-tests.override { withBenchmarks = true; };

              mesonComponentOverrides = finalAttrs: prevAttrs: {
                mesonFlags =
                  (prevAttrs.mesonFlags or [ ])
                  ++ lib.optionals withSanitizers [
                    # Run all tests with UBSAN enabled. Running both with ubsan and
                    # without doesn't seem to have much immediate benefit for doubling
                    # the GHA CI workaround.
                    #
                    # TODO: Work toward enabling "address,undefined" if it seems feasible.
                    # This would maybe require dropping Boost coroutines and ignoring intentional
                    # memory leaks with detect_leaks=0.
                    (lib.mesonOption "b_sanitize" "undefined")
                  ];
              };
            }
          );
    in
    (lib.concatMapAttrs (
      pkgName: pkg:
      lib.concatMapAttrs (testName: test: {
        "${componentTestsPrefix}${pkgName}-${testName}" = test;
      }) (pkg.tests or { })
    ) nixComponents)
    // lib.optionalAttrs (pkgs.stdenv.hostPlatform == pkgs.stdenv.buildPlatform) {
      "${componentTestsPrefix}nix-functional-tests" = nixComponents.nix-functional-tests;
    };
}
