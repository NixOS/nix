{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  pkgs ? nixFlake.inputs.nixpkgs.legacyPackages.${system},
  nixComponents ? (
    nixFlake.lib.makeComponents {
      inherit pkgs;
      inherit getStdenv;
    }
  ),
  getStdenv ? p: p.stdenv,
  componentTestsPrefix ? "",
  withSanitizers ? false,
  withCoverage ? false,
  ...
}:

let
  inherit (pkgs) lib;
  hydraJobs = nixFlake.hydraJobs;
  packages' = nixFlake.packages.${system};
  stdenv = (getStdenv pkgs);

  collectCoverageLayer = finalAttrs: prevAttrs: {
    env =
      let
        # https://clang.llvm.org/docs/SourceBasedCodeCoverage.html#the-code-coverage-workflow
        coverageFlags = [
          "-fprofile-instr-generate"
          "-fcoverage-mapping"
        ];
      in
      {
        CFLAGS = toString coverageFlags;
        CXXFLAGS = toString coverageFlags;
      };

    # Done in a pre-configure hook, because $NIX_BUILD_TOP needs to be substituted.
    preConfigure = prevAttrs.preConfigure or "" + ''
      mappingFlag=" -fcoverage-prefix-map=$NIX_BUILD_TOP/${finalAttrs.src.name}=${finalAttrs.src}"
      CFLAGS+="$mappingFlag"
      CXXFLAGS+="$mappingFlag"
    '';
  };

  componentOverrides = (lib.optional withCoverage collectCoverageLayer);
in

rec {
  nixComponentsInstrumented = nixComponents.overrideScope (
    final: prev: {
      withASan = withSanitizers;
      withUBSan = withSanitizers;

      nix-store-tests = prev.nix-store-tests.override { withBenchmarks = true; };
      # Boehm is incompatible with ASAN.
      nix-expr = prev.nix-expr.override { enableGC = !withSanitizers; };

      mesonComponentOverrides = lib.composeManyExtensions componentOverrides;
      # Unclear how to make Perl bindings work with a dynamically linked ASAN.
      nix-perl-bindings = if withSanitizers then null else prev.nix-perl-bindings;
    }
  );

  # Import NixOS tests using the instrumented components
  nixosTests = import ../../../tests/nixos {
    inherit lib pkgs;
    nixComponents = nixComponentsInstrumented;
    nixpkgs = nixFlake.inputs.nixpkgs;
    inherit (nixFlake.inputs) nixpkgs-23-11;
  };

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

  disable =
    let
      inherit (pkgs.stdenv) hostPlatform;
    in
    args@{
      pkgName,
      testName,
      test,
    }:
    lib.any (b: b) [
      # FIXME: Nix manual is impure and does not produce all settings on darwin
      (hostPlatform.isDarwin && pkgName == "nix-manual" && testName == "linkcheck")
    ];

  componentTests =
    (lib.concatMapAttrs (
      pkgName: pkg:
      lib.concatMapAttrs (
        testName: test:
        lib.optionalAttrs (!disable { inherit pkgName testName test; }) {
          "${componentTestsPrefix}${pkgName}-${testName}" = test;
        }
      ) (pkg.tests or { })
    ) nixComponentsInstrumented)
    // lib.optionalAttrs (pkgs.stdenv.hostPlatform == pkgs.stdenv.buildPlatform) {
      "${componentTestsPrefix}nix-functional-tests" = nixComponentsInstrumented.nix-functional-tests;
      "${componentTestsPrefix}nix-json-schema-checks" = nixComponentsInstrumented.nix-json-schema-checks;
    };

  codeCoverage =
    let
      componentsTestsToProfile =
        (builtins.mapAttrs (n: v: nixComponentsInstrumented.${n}.tests.run) {
          "nix-util-tests" = { };
          "nix-store-tests" = { };
          "nix-fetchers-tests" = { };
          "nix-expr-tests" = { };
          "nix-flake-tests" = { };
        })
        // {
          inherit (nixComponentsInstrumented) nix-functional-tests;
        };

      coverageProfileDrvs = lib.mapAttrs (
        n: v:
        v.overrideAttrs (
          finalAttrs: prevAttrs: {
            outputs = (prevAttrs.outputs or [ "out" ]) ++ [ "profraw" ];
            env = {
              LLVM_PROFILE_FILE = "${placeholder "profraw"}/%m";
            };
          }
        )
      ) componentsTestsToProfile;

      coverageProfiles = lib.mapAttrsToList (n: v: lib.getOutput "profraw" v) coverageProfileDrvs;

      mergedProfdata =
        pkgs.runCommand "merged-profdata"
          {
            __structuredAttrs = true;
            nativeBuildInputs = [ pkgs.llvmPackages.libllvm ];
            inherit coverageProfiles;
          }
          ''
            rawProfiles=()
            for dir in "''\${coverageProfiles[@]}"; do
              rawProfiles+=($dir/*)
            done
            llvm-profdata merge -sparse -output $out "''\${rawProfiles[@]}"
          '';

      coverageReports =
        let
          nixComponentDrvs = lib.filter (lib.isDerivation) (lib.attrValues nixComponentsInstrumented);
        in
        pkgs.runCommand "code-coverage-report"
          {
            nativeBuildInputs = [
              pkgs.llvmPackages.libllvm
              pkgs.jq
            ];
            __structuredAttrs = true;
            nixComponents = nixComponentDrvs;
          }
          ''
            # ${toString (lib.map (v: v.src) nixComponentDrvs)}

            binaryFiles=()
            for dir in "''\${nixComponents[@]}"; do
              readarray -t filesInDir < <(find "$dir" -type f -executable)
              binaryFiles+=("''\${filesInDir[@]}")
            done

            arguments=$(concatStringsSep " -object " binaryFiles)
            llvm-cov show $arguments -instr-profile ${mergedProfdata} -output-dir $out -format=html

            {
              echo "# Code coverage summary (generated via \`llvm-cov\`):"
              echo
              echo '```'
              llvm-cov report $arguments -instr-profile ${mergedProfdata} -format=text -use-color=false
              echo '```'
              echo
            } >> $out/index.txt

            llvm-cov export $arguments -instr-profile ${mergedProfdata} -format=text > $out/coverage.json

            mkdir -p $out/nix-support

            coverageTotals=$(jq ".data[0].totals" $out/coverage.json)

            # Mostly inline from pkgs/build-support/setup-hooks/make-coverage-analysis-report.sh [1],
            # which we can't use here, because we rely on LLVM's infra for source code coverage collection.
            # [1]: https://github.com/NixOS/nixpkgs/blob/67bb48c4c8e327417d6d5aa7e538244b209e852b/pkgs/build-support/setup-hooks/make-coverage-analysis-report.sh#L16
            declare -A metricsArray=(["lineCoverage"]="lines" ["functionCoverage"]="functions" ["branchCoverage"]="branches")

            for metricName in "''\${!metricsArray[@]}"; do
              key="''\${metricsArray[$metricName]}"
              metric=$(echo "$coverageTotals" | jq ".$key.percent * 10 | round / 10")
              echo "$metricName $metric %" >> $out/nix-support/hydra-metrics
            done

            echo "report coverage $out" >> $out/nix-support/hydra-build-products
          '';
    in
    assert withCoverage;
    assert stdenv.cc.isClang;
    {
      inherit coverageProfileDrvs mergedProfdata coverageReports;
    };

  vmTests = {
    inherit (nixosTests) s3-binary-cache-store;
  }
  // lib.optionalAttrs (!withSanitizers && !withCoverage) {
    # evalNixpkgs uses non-instrumented components from hydraJobs, so only run it
    # when not testing with sanitizers to avoid rebuilding nix
    inherit (hydraJobs.tests) evalNixpkgs;
    # FIXME: CI times out when building vm tests instrumented
    inherit (nixosTests)
      functional_user
      githubFlakes
      nix-docker
      tarballFlakes
      ;
  };
}
