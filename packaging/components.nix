scope:
let
  inherit (scope) callPackage;
in

# This becomes the pkgs.nixComponents attribute set
{
  nix = callPackage ../package.nix { };

  nix-util = callPackage ../src/libutil/package.nix { };
  nix-util-c = callPackage ../src/libutil-c/package.nix { };
  nix-util-test-support = callPackage ../src/nix-util-test-support/package.nix { };
  nix-util-tests = callPackage ../src/nix-util-tests/package.nix { };

  nix-store = callPackage ../src/libstore/package.nix { };
  nix-store-c = callPackage ../src/libstore-c/package.nix { };
  nix-store-test-support = callPackage ../src/nix-store-test-support/package.nix { };
  nix-store-tests = callPackage ../src/nix-store-tests/package.nix { };

  nix-fetchers = callPackage ../src/libfetchers/package.nix { };
  nix-fetchers-tests = callPackage ../src/nix-fetchers-tests/package.nix { };

  nix-expr = callPackage ../src/libexpr/package.nix { };
  nix-expr-c = callPackage ../src/libexpr-c/package.nix { };
  nix-expr-test-support = callPackage ../src/nix-expr-test-support/package.nix { };
  nix-expr-tests = callPackage ../src/nix-expr-tests/package.nix { };

  nix-flake = callPackage ../src/libflake/package.nix { };
  nix-flake-tests = callPackage ../src/nix-flake-tests/package.nix { };

  nix-internal-api-docs = callPackage ../src/internal-api-docs/package.nix { };
  nix-external-api-docs = callPackage ../src/external-api-docs/package.nix { };

  nix-perl-bindings = callPackage ../src/perl/package.nix { };
}
