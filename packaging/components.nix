scope:
let
  inherit (scope) callPackage;
in

# This becomes the pkgs.nixComponents attribute set
{
  nix = callPackage ../package.nix { };

  nix-util = callPackage ../src/libutil/package.nix { };
  nix-util-c = callPackage ../src/libutil-c/package.nix { };
  nix-util-test-support = callPackage ../src/libutil-test-support/package.nix { };
  nix-util-test = callPackage ../src/libutil-test/package.nix { };

  nix-store = callPackage ../src/libstore/package.nix { };
  nix-store-c = callPackage ../src/libstore-c/package.nix { };
  nix-store-test-support = callPackage ../src/libstore-test-support/package.nix { };
  nix-store-test = callPackage ../src/libstore-test/package.nix { };

  nix-fetchers = callPackage ../src/libfetchers/package.nix { };
  nix-fetchers-test = callPackage ../src/libfetchers-test/package.nix { };

  nix-expr = callPackage ../src/libexpr/package.nix { };
  nix-expr-c = callPackage ../src/libexpr-c/package.nix { };
  nix-expr-test-support = callPackage ../src/libexpr-test-support/package.nix { };
  nix-expr-test = callPackage ../src/libexpr-test/package.nix { };

  nix-flake = callPackage ../src/libflake/package.nix { };
  nix-flake-test = callPackage ../src/libflake-test/package.nix { };

  nix-internal-api-docs = callPackage ../src/internal-api-docs/package.nix { };
  nix-external-api-docs = callPackage ../src/external-api-docs/package.nix { };

  nix-perl-bindings = callPackage ../src/perl/package.nix { };
}
