{
  lib,
  stdenv,
  buildEnv,

  nix-util,
  nix-util-c,
  nix-util-test-support,
  nix-util-tests,

  nix-store,
  nix-store-c,
  nix-store-test-support,
  nix-store-tests,

  nix-fetchers,
  nix-fetchers-tests,

  nix-expr,
  nix-expr-c,
  nix-expr-test-support,
  nix-expr-tests,

  nix-flake,
  nix-flake-tests,

  nix-main,
  nix-main-c,

  nix-cmd,

  nix-cli,

  nix-functional-tests,

  nix-internal-api-docs,
  nix-external-api-docs,

  nix-perl-bindings,
}:

(buildEnv rec {
  name = "nix-${nix-cli.version}";
  paths = [
    nix-util
    nix-util-c
    nix-util-test-support
    nix-util-tests

    nix-store
    nix-store-c
    nix-store-test-support
    nix-store-tests

    nix-fetchers
    nix-fetchers-tests

    nix-expr
    nix-expr-c
    nix-expr-test-support
    nix-expr-tests

    nix-flake
    nix-flake-tests

    nix-main
    nix-main-c

    nix-cmd

    nix-cli

    nix-internal-api-docs
    nix-external-api-docs

  ] ++ lib.optionals (stdenv.buildPlatform.canExecute stdenv.hostPlatform) [
    nix-perl-bindings
  ];
}).overrideAttrs (_: {
  doCheck = true;
  doInstallCheck = true;

  checkInputs = [
    # Actually run the unit tests too
    nix-util-tests.tests.run
    nix-store-tests.tests.run
    nix-expr-tests.tests.run
    nix-flake-tests.tests.run
  ];
  installCheckInputs = [
    nix-functional-tests
  ];
})
