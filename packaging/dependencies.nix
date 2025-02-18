# These overrides are applied to the dependencies of the Nix components.

{
  # Flake inputs; used for sources
  inputs,

  # The raw Nixpkgs, not affected by this scope
  pkgs,

  stdenv,
}:

let
  prevStdenv = stdenv;
in

let
  inherit (pkgs) lib;

  stdenv = if prevStdenv.isDarwin && prevStdenv.isx86_64 then darwinStdenv else prevStdenv;

  # Fix the following error with the default x86_64-darwin SDK:
  #
  #     error: aligned allocation function of type 'void *(std::size_t, std::align_val_t)' is only available on macOS 10.13 or newer
  #
  # Despite the use of the 10.13 deployment target here, the aligned
  # allocation function Clang uses with this setting actually works
  # all the way back to 10.6.
  darwinStdenv = pkgs.overrideSDK prevStdenv { darwinMinVersion = "10.13"; };

in
scope: {
  inherit stdenv;

  aws-sdk-cpp =
    (pkgs.aws-sdk-cpp.override {
      apis = [
        "s3"
        "transfer"
      ];
      customMemoryManagement = false;
    }).overrideAttrs
      {
        # only a stripped down version is built, which takes a lot less resources
        # to build, so we don't need a "big-parallel" machine.
        requiredSystemFeatures = [ ];
      };

  boehmgc = pkgs.boehmgc.override {
    enableLargeConfig = true;
  };

  # TODO Hack until https://github.com/NixOS/nixpkgs/issues/45462 is fixed.
  boost =
    (pkgs.boost.override {
      extraB2Args = [
        "--with-container"
        "--with-context"
        "--with-coroutine"
      ];
    }).overrideAttrs
      (old: {
        # Need to remove `--with-*` to use `--with-libraries=...`
        buildPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.buildPhase;
        installPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.installPhase;
      });

  libgit2 = pkgs.libgit2.overrideAttrs (
    attrs:
    {
      cmakeFlags = attrs.cmakeFlags or [ ] ++ [ "-DUSE_SSH=exec" ];
    }
    # libgit2: Nixpkgs 24.11 has < 1.9.0, which needs our patches
    // lib.optionalAttrs (!lib.versionAtLeast pkgs.libgit2.version "1.9.0") {
      nativeBuildInputs =
        attrs.nativeBuildInputs or [ ]
        # gitMinimal does not build on Windows. See packbuilder patch.
        ++ lib.optionals (!stdenv.hostPlatform.isWindows) [
          # Needed for `git apply`; see `prePatch`
          pkgs.buildPackages.gitMinimal
        ];
      # Only `git apply` can handle git binary patches
      prePatch =
        attrs.prePatch or ""
        + lib.optionalString (!stdenv.hostPlatform.isWindows) ''
          patch() {
            git apply
          }
        '';
      patches =
        attrs.patches or [ ]
        ++ [
          ./patches/libgit2-mempack-thin-packfile.patch
        ]
        # gitMinimal does not build on Windows, but fortunately this patch only
        # impacts interruptibility
        ++ lib.optionals (!stdenv.hostPlatform.isWindows) [
          # binary patch; see `prePatch`
          ./patches/libgit2-packbuilder-callback-interruptible.patch
        ];
    }
  );
}
