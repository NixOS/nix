{pkgs, stdenv, versionSuffix}: scope:
let
  inherit (scope) callPackage;
in

# This becomes the pkgs.nixComponents attribute set
{
  inherit stdenv versionSuffix;
  libseccomp = pkgs.libseccomp_nix;
  boehmgc = pkgs.boehmgc_nix;
  libgit2 = pkgs.libgit2_nix;
  busybox-sandbox-shell = pkgs.busybox-sandbox-shell or pkgs.default-busybox-sandbox-shell;

  nix = callPackage ../package.nix { };

  nix-util = callPackage ../src/libutil/package.nix { };

  nix-util-test-support = callPackage ../tests/unit/libutil-support/package.nix { };

  nix-util-test = callPackage ../tests/unit/libutil/package.nix { };

  nix-util-c = callPackage ../src/libutil-c/package.nix { };

  nix-store = callPackage ../src/libstore/package.nix { };

  nix-fetchers = callPackage ../src/libfetchers/package.nix { };

  nix-perl-bindings = callPackage ../src/perl/package.nix { };
}
