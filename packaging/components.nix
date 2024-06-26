scope:
let
  inherit (scope) callPackage;
in

# This becomes the pkgs.nixComponents attribute set
{
  nix = callPackage ../package.nix { };

  nix-util = callPackage ../src/libutil/package.nix { };

  nix-util-test-support = callPackage ../tests/unit/libutil-support/package.nix { };

  nix-util-test = callPackage ../tests/unit/libutil/package.nix { };

  nix-util-c = callPackage ../src/libutil-c/package.nix { };

  nix-store = callPackage ../src/libstore/package.nix { };

  nix-fetchers = callPackage ../src/libfetchers/package.nix { };

  nix-perl-bindings = callPackage ../src/perl/package.nix { };

  nix-internal-api-docs = callPackage ../src/internal-api-docs/package.nix { };

  nix-external-api-docs = callPackage ../src/external-api-docs/package.nix { };

}
