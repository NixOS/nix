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
  # NOTE: this is not just a version constraint, but a request to make Darwin
  #       provide this version level of support. Removing this minimum version
  #       request will regress the above error.
  darwinStdenv = pkgs.overrideSDK prevStdenv { darwinMinVersion = "10.13"; };

in
scope: {
  inherit stdenv;

  aws-sdk-cpp =
    (pkgs.aws-sdk-cpp.override {
      apis = [
        "identity-management"
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

  boehmgc =
    (pkgs.boehmgc.override {
      enableLargeConfig = true;
    }).overrideAttrs
      (attrs: {
        # Increase the initial mark stack size to avoid stack
        # overflows, since these inhibit parallel marking (see
        # GC_mark_some()). To check whether the mark stack is too
        # small, run Nix with GC_PRINT_STATS=1 and look for messages
        # such as `Mark stack overflow`, `No room to copy back mark
        # stack`, and `Grew mark stack to ... frames`.
        NIX_CFLAGS_COMPILE = "-DINITIAL_MARK_STACK_SIZE=1048576";
      });

  lowdown = pkgs.lowdown.overrideAttrs (prevAttrs: rec {
    version = "2.0.2";
    src = pkgs.fetchurl {
      url = "https://kristaps.bsd.lv/lowdown/snapshots/lowdown-${version}.tar.gz";
      hash = "sha512-cfzhuF4EnGmLJf5EGSIbWqJItY3npbRSALm+GarZ7SMU7Hr1xw0gtBFMpOdi5PBar4TgtvbnG4oRPh+COINGlA==";
    };
    nativeBuildInputs = prevAttrs.nativeBuildInputs ++ [ pkgs.buildPackages.bmake ];
    postInstall =
      lib.replaceStrings [ "lowdown.so.1" "lowdown.1.dylib" ] [ "lowdown.so.2" "lowdown.2.dylib" ]
        prevAttrs.postInstall;
  });

  # TODO Hack until https://github.com/NixOS/nixpkgs/issues/45462 is fixed.
  boost =
    (pkgs.boost.override {
      extraB2Args = [
        "--with-container"
        "--with-context"
        "--with-coroutine"
        "--with-iostreams"
        "--with-url"
      ];
      enableIcu = false;
    }).overrideAttrs
      (old: {
        # Need to remove `--with-*` to use `--with-libraries=...`
        buildPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.buildPhase;
        installPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.installPhase;
      });

  libgit2 =
    if lib.versionAtLeast pkgs.libgit2.version "1.9.0" then
      pkgs.libgit2
    else
      pkgs.libgit2.overrideAttrs (attrs: {
        # libgit2: Nixpkgs 24.11 has < 1.9.0, which needs our patches
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
      });
}
