# These overrides are applied to the dependencies of the Nix components.

{
  # Flake inputs; used for sources
  inputs,

  # The raw Nixpkgs, not affected by this scope
  pkgs,

  stdenv,
  versionSuffix,
}:
scope: {
  inherit stdenv versionSuffix;

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

  inherit (inputs) flake-schemas;
}
