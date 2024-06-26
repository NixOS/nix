{pkgs, stdenv, officialRelease, versionSuffix}: scope:
let
  inherit (scope) callPackage;

  # TODO: push fileset parameter into package.nix files as `lib` parameter
  inherit (callPackage (args@{ lib }: args) {}) lib;
  inherit (lib) fileset;
in

# This becomes the pkgs.nixComponents attribute set
{
  # TODO: build the nix CLI with meson
  nix = pkgs.callPackage ../package.nix {
    inherit
      fileset
      stdenv
      officialRelease
      versionSuffix
      ;
    boehmgc = pkgs.boehmgc-nix;
    libgit2 = pkgs.libgit2-nix;
    libseccomp = pkgs.libseccomp-nix;
    busybox-sandbox-shell = pkgs.busybox-sandbox-shell or pkgs.default-busybox-sandbox-shell;
  };

  nix-util = callPackage ../src/libutil/package.nix {
    inherit
      fileset
      stdenv
      officialRelease
      versionSuffix
      ;
  };

  nix-util-test-support = callPackage ../tests/unit/libutil-support/package.nix {
    inherit
      fileset
      stdenv
      versionSuffix
      ;
  };

  nix-util-test = callPackage ../tests/unit/libutil/package.nix {
    inherit
      fileset
      stdenv
      versionSuffix
      ;
  };

  nix-util-c = callPackage ../src/libutil-c/package.nix {
    inherit
      fileset
      stdenv
      versionSuffix
      ;
  };

  nix-store = callPackage ../src/libstore/package.nix {
    inherit
      fileset
      stdenv
      officialRelease
      versionSuffix
      ;
    libseccomp = pkgs.libseccomp-nix;
    busybox-sandbox-shell = pkgs.busybox-sandbox-shell or pkgs.default-busybox-sandbox-shell;
  };

  nix-fetchers = callPackage ../src/libfetchers/package.nix {
    inherit
      fileset
      stdenv
      officialRelease
      versionSuffix
      ;
  };

  nix-perl-bindings = callPackage ../src/perl/package.nix {
    inherit
      fileset
      stdenv
      versionSuffix
      ;
  };
}
