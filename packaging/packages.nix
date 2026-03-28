{
  lib,
  self,
  nixpkgsFor,
  flatMapAttrs,
  forAllSystems,
  forAllCrossSystems,
  stdenvs,
  crossSystems,
  linux64BitSystems,
}:
forAllSystems (
  system:
  {
    # Here we put attributes that map 1:1 into packages.<system>, ie
    # for which we don't apply the full build matrix such as cross or static.
    inherit (nixpkgsFor.${system}.native)
      changelog-d
      ;
    default = self.packages.${system}.nix;
    installerScriptForGHA = self.hydraJobs.installerScriptForGHA.${system};
    binaryTarball = self.hydraJobs.binaryTarball.${system};
    # TODO probably should be `nix-cli`
    nix = self.packages.${system}.nix-everything;
    nix-manual = nixpkgsFor.${system}.native.nixComponents2.nix-manual;
    nix-manual-manpages-only = nixpkgsFor.${system}.native.nixComponents2.nix-manual-manpages-only;
    nix-internal-api-docs = nixpkgsFor.${system}.native.nixComponents2.nix-internal-api-docs;
    nix-external-api-docs = nixpkgsFor.${system}.native.nixComponents2.nix-external-api-docs;
  }
  # We need to flatten recursive attribute sets of derivations to pass `flake check`.
  //
    flatMapAttrs
      {
        # Components we'll iterate over in the upcoming lambda
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

        "nix-nswrapper" = {
          linuxOnly = true;
        };

        "nix-cli" = { };

        "nix-everything" = { };

        "nix-functional-tests" = {
          supportsCross = false;
        };

        "nix-json-schema-checks" = {
          supportsCross = false;
        };

        "nix-perl-bindings" = {
          supportsCross = false;
        };

        "nix-clang-tidy-plugin" = {
          supportsCross = false;
        };
      }
      (
        pkgName:
        {
          supportsCross ? true,
          linuxOnly ? false,
        }:
        lib.optionalAttrs (linuxOnly -> nixpkgsFor.${system}.native.stdenv.hostPlatform.isLinux) (
          {
            # These attributes go right into `packages.<system>`.
            "${pkgName}" = nixpkgsFor.${system}.native.nixComponents2.${pkgName};
            "${pkgName}-static" = nixpkgsFor.${system}.native.pkgsStatic.nixComponents2.${pkgName};
            "${pkgName}-llvm" = nixpkgsFor.${system}.native.pkgsLLVM.nixComponents2.${pkgName};
          }
          // flatMapAttrs (lib.genAttrs stdenvs (_: { })) (
            stdenvName:
            { }:
            {
              # These attributes go right into `packages.<system>`.
              "${pkgName}-${stdenvName}" =
                nixpkgsFor.${system}.nativeForStdenv.${stdenvName}.nixComponents2.${pkgName};
            }
          )
        )
        // lib.optionalAttrs supportsCross (
          flatMapAttrs (lib.genAttrs crossSystems (_: { })) (
            crossSystem:
            { }:
            lib.optionalAttrs
              (linuxOnly -> nixpkgsFor.${system}.cross.${crossSystem}.stdenv.hostPlatform.isLinux)
              {
                # These attributes go right into `packages.<system>`.
                "${pkgName}-${crossSystem}" = nixpkgsFor.${system}.cross.${crossSystem}.nixComponents2.${pkgName};
              }
          )
        )
      )
  // lib.optionalAttrs (builtins.elem system linux64BitSystems) {
    dockerImage =
      let
        pkgs = nixpkgsFor.${system}.native;
        image = pkgs.callPackage ./docker.nix {
          tag = pkgs.nix.version;
        };
      in
      pkgs.runCommand "docker-image-tarball-${pkgs.nix.version}"
        { meta.description = "Docker image with Nix for ${system}"; }
        ''
          mkdir -p $out/nix-support
          image=$out/image.tar.gz
          ln -s ${image} $image
          echo "file binary-dist $image" >> $out/nix-support/hydra-build-products
        '';
  }
)
