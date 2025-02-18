{
  lib,
  pkgs,
  src,
  stdenv,
  officialRelease,
}:

scope:

let
  inherit (scope)
    callPackage
    ;
  inherit (pkgs.buildPackages)
    meson
    ninja
    pkg-config
    ;

  baseVersion = lib.fileContents ../.version;

  versionSuffix = lib.optionalString (!officialRelease) "pre";

  fineVersionSuffix =
    lib.optionalString (!officialRelease)
      "pre${
        builtins.substring 0 8 (src.lastModifiedDate or src.lastModified or "19700101")
      }_${src.shortRev or "dirty"}";

  fineVersion = baseVersion + fineVersionSuffix;

  root = ../.;

  # Nixpkgs implements this by returning a subpath into the fetched Nix sources.
  resolvePath = p: p;

  # Indirection for Nixpkgs to override when package.nix files are vendored
  filesetToSource = lib.fileset.toSource;

  /**
    Given a set of layers, create a mkDerivation-like function
  */
  mkPackageBuilder =
    exts: userFn: stdenv.mkDerivation (lib.extends (lib.composeManyExtensions exts) userFn);

  setVersionLayer = finalAttrs: prevAttrs: {
    preConfigure =
      prevAttrs.prevAttrs or ""
      +
        # Update the repo-global .version file.
        # Symlink ./.version points there, but by default only workDir is writable.
        ''
          chmod u+w ./.version
          echo ${finalAttrs.version} > ./.version
        '';
  };

  localSourceLayer =
    finalAttrs: prevAttrs:
    let
      workDirPath =
        # Ideally we'd pick finalAttrs.workDir, but for now `mkDerivation` has
        # the requirement that everything except passthru and meta must be
        # serialized by mkDerivation, which doesn't work for this.
        prevAttrs.workDir;

      workDirSubpath = lib.path.removePrefix root workDirPath;
      sources =
        assert prevAttrs.fileset._type == "fileset";
        prevAttrs.fileset;
      src = lib.fileset.toSource {
        fileset = sources;
        inherit root;
      };

    in
    {
      sourceRoot = "${src.name}/" + workDirSubpath;
      inherit src;

      # Clear what `derivation` can't/shouldn't serialize; see prevAttrs.workDir.
      fileset = null;
      workDir = null;
    };

  mesonLayer = finalAttrs: prevAttrs: {
    # NOTE:
    # As of https://github.com/NixOS/nixpkgs/blob/8baf8241cea0c7b30e0b8ae73474cb3de83c1a30/pkgs/by-name/me/meson/setup-hook.sh#L26,
    # `mesonBuildType` defaults to `plain` if not specified. We want our Nix-built binaries to be optimized by default.
    # More on build types here: https://mesonbuild.com/Builtin-options.html#details-for-buildtype.
    mesonBuildType = "release";
    # NOTE:
    # Users who are debugging Nix builds are expected to set the environment variable `mesonBuildType`, per the
    # guidance in https://github.com/NixOS/nix/blob/8a3fc27f1b63a08ac983ee46435a56cf49ebaf4a/doc/manual/source/development/debugging.md?plain=1#L10.
    # For this reason, we don't want to refer to `finalAttrs.mesonBuildType` here, but rather use the environment variable.
    preConfigure =
      prevAttrs.preConfigure or ""
      +
        lib.optionalString
          (
            !stdenv.hostPlatform.isWindows
            # build failure
            && !stdenv.hostPlatform.isStatic
            # LTO breaks exception handling on x86-64-darwin.
            && stdenv.system != "x86_64-darwin"
          )
          ''
            case "$mesonBuildType" in
            release|minsize) appendToVar mesonFlags "-Db_lto=true"  ;;
            *)               appendToVar mesonFlags "-Db_lto=false" ;;
            esac
          '';
    nativeBuildInputs = [
      meson
      ninja
    ] ++ prevAttrs.nativeBuildInputs or [ ];
    mesonCheckFlags = prevAttrs.mesonCheckFlags or [ ] ++ [
      "--print-errorlogs"
    ];
  };

  mesonBuildLayer = finalAttrs: prevAttrs: {
    nativeBuildInputs = prevAttrs.nativeBuildInputs or [ ] ++ [
      pkg-config
    ];
    separateDebugInfo = !stdenv.hostPlatform.isStatic;
    hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";
    env =
      prevAttrs.env or { }
      // lib.optionalAttrs (
        stdenv.isLinux
        && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")
        && !(stdenv.hostPlatform.useLLVM or false)
      ) { LDFLAGS = "-fuse-ld=gold"; };
  };

  mesonLibraryLayer = finalAttrs: prevAttrs: {
    outputs = prevAttrs.outputs or [ "out" ] ++ [ "dev" ];
  };

  # Work around weird `--as-needed` linker behavior with BSD, see
  # https://github.com/mesonbuild/meson/issues/3593
  bsdNoLinkAsNeeded =
    finalAttrs: prevAttrs:
    lib.optionalAttrs stdenv.hostPlatform.isBSD {
      mesonFlags = [ (lib.mesonBool "b_asneeded" false) ] ++ prevAttrs.mesonFlags or [ ];
    };

  miscGoodPractice = finalAttrs: prevAttrs: {
    strictDeps = prevAttrs.strictDeps or true;
    enableParallelBuilding = true;
  };

in

# This becomes the pkgs.nixComponents attribute set
{
  version = baseVersion + versionSuffix;
  inherit versionSuffix;

  inherit resolvePath filesetToSource;

  mkMesonDerivation = mkPackageBuilder [
    miscGoodPractice
    localSourceLayer
    setVersionLayer
    mesonLayer
  ];
  mkMesonExecutable = mkPackageBuilder [
    miscGoodPractice
    bsdNoLinkAsNeeded
    localSourceLayer
    setVersionLayer
    mesonLayer
    mesonBuildLayer
  ];
  mkMesonLibrary = mkPackageBuilder [
    miscGoodPractice
    bsdNoLinkAsNeeded
    localSourceLayer
    mesonLayer
    setVersionLayer
    mesonBuildLayer
    mesonLibraryLayer
  ];

  nix-util = callPackage ../src/libutil/package.nix { };
  nix-util-c = callPackage ../src/libutil-c/package.nix { };
  nix-util-test-support = callPackage ../src/libutil-test-support/package.nix { };
  nix-util-tests = callPackage ../src/libutil-tests/package.nix { };

  nix-store = callPackage ../src/libstore/package.nix { };
  nix-store-c = callPackage ../src/libstore-c/package.nix { };
  nix-store-test-support = callPackage ../src/libstore-test-support/package.nix { };
  nix-store-tests = callPackage ../src/libstore-tests/package.nix { };

  nix-fetchers = callPackage ../src/libfetchers/package.nix { };
  nix-fetchers-tests = callPackage ../src/libfetchers-tests/package.nix { };

  nix-expr = callPackage ../src/libexpr/package.nix { };
  nix-expr-c = callPackage ../src/libexpr-c/package.nix { };
  nix-expr-test-support = callPackage ../src/libexpr-test-support/package.nix { };
  nix-expr-tests = callPackage ../src/libexpr-tests/package.nix { };

  nix-flake = callPackage ../src/libflake/package.nix { };
  nix-flake-c = callPackage ../src/libflake-c/package.nix { };
  nix-flake-tests = callPackage ../src/libflake-tests/package.nix { };

  nix-main = callPackage ../src/libmain/package.nix { };
  nix-main-c = callPackage ../src/libmain-c/package.nix { };

  nix-cmd = callPackage ../src/libcmd/package.nix { };

  nix-cli = callPackage ../src/nix/package.nix { version = fineVersion; };

  nix-functional-tests = callPackage ../tests/functional/package.nix {
    version = fineVersion;
  };

  nix-manual = callPackage ../doc/manual/package.nix { version = fineVersion; };
  nix-internal-api-docs = callPackage ../src/internal-api-docs/package.nix { version = fineVersion; };
  nix-external-api-docs = callPackage ../src/external-api-docs/package.nix { version = fineVersion; };

  nix-perl-bindings = callPackage ../src/perl/package.nix { };

  nix-everything = callPackage ../packaging/everything.nix { };
}
