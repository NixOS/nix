# These overrides are applied to the dependencies of the Nix components.

{
  # Flake inputs; used for sources
  inputs,

  # The raw Nixpkgs, not affected by this scope
  pkgs,

  stdenv,
  versionSuffix,
}:

let
  inherit (pkgs) lib;

  localSourceLayer = finalAttrs: prevAttrs:
    let
      root = ../.;
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

in
scope: {
  inherit stdenv versionSuffix;
  version = lib.fileContents ../.version + versionSuffix;

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

  mkMesonDerivation = f: stdenv.mkDerivation (lib.extends localSourceLayer f);
}
