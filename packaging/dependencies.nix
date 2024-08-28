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

  root = ../.;

  stdenv = if prevStdenv.isDarwin && prevStdenv.isx86_64
    then darwinStdenv
    else prevStdenv;

  # Fix the following error with the default x86_64-darwin SDK:
  #
  #     error: aligned allocation function of type 'void *(std::size_t, std::align_val_t)' is only available on macOS 10.13 or newer
  #
  # Despite the use of the 10.13 deployment target here, the aligned
  # allocation function Clang uses with this setting actually works
  # all the way back to 10.6.
  darwinStdenv = pkgs.overrideSDK prevStdenv { darwinMinVersion = "10.13"; };

  # Nixpkgs implements this by returning a subpath into the fetched Nix sources.
  resolvePath = p: p;

  # Indirection for Nixpkgs to override when package.nix files are vendored
  filesetToSource = lib.fileset.toSource;

  localSourceLayer = finalAttrs: prevAttrs:
    let
      workDirPath =
        # Ideally we'd pick finalAttrs.workDir, but for now `mkDerivation` has
        # the requirement that everything except passthru and meta must be
        # serialized by mkDerivation, which doesn't work for this.
        prevAttrs.workDir;

      workDirSubpath = lib.path.removePrefix root workDirPath;
      sources = assert prevAttrs.fileset._type == "fileset"; prevAttrs.fileset;
      src = lib.fileset.toSource { fileset = sources; inherit root; };

    in
    {
      sourceRoot = "${src.name}/" + workDirSubpath;
      inherit src;

      # Clear what `derivation` can't/shouldn't serialize; see prevAttrs.workDir.
      fileset = null;
      workDir = null;
    };

  # Work around weird `--as-needed` linker behavior with BSD, see
  # https://github.com/mesonbuild/meson/issues/3593
  bsdNoLinkAsNeeded = finalAttrs: prevAttrs:
    lib.optionalAttrs stdenv.hostPlatform.isBSD {
      mesonFlags = [ (lib.mesonBool "b_asneeded" false) ] ++ prevAttrs.mesonFlags or [];
    };

  miscGoodPractice = finalAttrs: prevAttrs:
    {
      strictDeps = prevAttrs.strictDeps or true;
      enableParallelBuilding = true;
    };
in
scope: {
  inherit stdenv;

  aws-sdk-cpp = (pkgs.aws-sdk-cpp.override {
    apis = [ "s3" "transfer" ];
    customMemoryManagement = false;
  }).overrideAttrs {
    # only a stripped down version is built, which takes a lot less resources
    # to build, so we don't need a "big-parallel" machine.
    requiredSystemFeatures = [ ];
  };

  libseccomp = pkgs.libseccomp.overrideAttrs (_: rec {
    version = "2.5.5";
    src = pkgs.fetchurl {
      url = "https://github.com/seccomp/libseccomp/releases/download/v${version}/libseccomp-${version}.tar.gz";
      hash = "sha256-JIosik2bmFiqa69ScSw0r+/PnJ6Ut23OAsHJqiX7M3U=";
    };
  });

  boehmgc = pkgs.boehmgc.override {
    enableLargeConfig = true;
  };

  # TODO Hack until https://github.com/NixOS/nixpkgs/issues/45462 is fixed.
  boost = (pkgs.boost.override {
    extraB2Args = [
      "--with-container"
      "--with-context"
      "--with-coroutine"
    ];
  }).overrideAttrs (old: {
    # Need to remove `--with-*` to use `--with-libraries=...`
    buildPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.buildPhase;
    installPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.installPhase;
  });

  libgit2 = pkgs.libgit2.overrideAttrs (attrs: {
    src = inputs.libgit2;
    version = inputs.libgit2.lastModifiedDate;
    cmakeFlags = attrs.cmakeFlags or []
      ++ [ "-DUSE_SSH=exec" ];
    nativeBuildInputs = attrs.nativeBuildInputs or []
      # gitMinimal does not build on Windows. See packbuilder patch.
      ++ lib.optionals (!stdenv.hostPlatform.isWindows) [
        # Needed for `git apply`; see `prePatch`
        pkgs.buildPackages.gitMinimal
      ];
    # Only `git apply` can handle git binary patches
    prePatch = attrs.prePatch or ""
      + lib.optionalString (!stdenv.hostPlatform.isWindows) ''
        patch() {
          git apply
        }
      '';
    patches = attrs.patches or []
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

  busybox-sandbox-shell = pkgs.busybox-sandbox-shell or (pkgs.busybox.override {
    useMusl = true;
    enableStatic = true;
    enableMinimal = true;
    extraConfig = ''
      CONFIG_FEATURE_FANCY_ECHO y
      CONFIG_FEATURE_SH_MATH y
      CONFIG_FEATURE_SH_MATH_64 y

      CONFIG_ASH y
      CONFIG_ASH_OPTIMIZE_FOR_SIZE y

      CONFIG_ASH_ALIAS y
      CONFIG_ASH_BASH_COMPAT y
      CONFIG_ASH_CMDCMD y
      CONFIG_ASH_ECHO y
      CONFIG_ASH_GETOPTS y
      CONFIG_ASH_INTERNAL_GLOB y
      CONFIG_ASH_JOB_CONTROL y
      CONFIG_ASH_PRINTF y
      CONFIG_ASH_TEST y
    '';
  });

  # TODO change in Nixpkgs, Windows works fine. First commit of
  # https://github.com/NixOS/nixpkgs/pull/322977 backported will fix.
  toml11 = pkgs.toml11.overrideAttrs (old: {
    meta.platforms = lib.platforms.all;
  });

  inherit resolvePath filesetToSource;

  mkMesonDerivation = f: let
    exts = [
      miscGoodPractice
      bsdNoLinkAsNeeded
      localSourceLayer
    ];
  in stdenv.mkDerivation
   (lib.extends
     (lib.foldr lib.composeExtensions (_: _: {}) exts)
     f);
}
