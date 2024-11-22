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
  nix-flake-c,
  nix-flake-tests,

  nix-main,
  nix-main-c,

  nix-cmd,

  nix-cli,

  nix-functional-tests,

  nix-manual,
  nix-internal-api-docs,
  nix-external-api-docs,

  nix-perl-bindings,
}:

<<<<<<< HEAD
=======
let
  dev = stdenv.mkDerivation (finalAttrs: {
    name = "nix-${nix-cli.version}-dev";
    pname = "nix";
    version = nix-cli.version;
    dontUnpack = true;
    dontBuild = true;
    libs = map lib.getDev [
      nix-cmd
      nix-expr
      nix-expr-c
      nix-fetchers
      nix-flake
      nix-flake-c
      nix-main
      nix-main-c
      nix-store
      nix-store-c
      nix-util
      nix-util-c
      nix-perl-bindings
    ];
    installPhase = ''
      mkdir -p $out/nix-support
      echo $libs >> $out/nix-support/propagated-build-inputs
    '';
    passthru = {
      tests = {
        pkg-config =
          testers.hasPkgConfigModules {
            package = finalAttrs.finalPackage;
          };
      };

      # If we were to fully emulate output selection here, we'd confuse the Nix CLIs,
      # because they rely on `drvPath`.
      dev = finalAttrs.finalPackage.out;

      libs = throw "`nix.dev.libs` is not meant to be used; use `nix.libs` instead.";
    };
    meta = {
      pkgConfigModules = [
        "nix-cmd"
        "nix-expr"
        "nix-expr-c"
        "nix-fetchers"
        "nix-flake"
        "nix-flake-c"
        "nix-main"
        "nix-main-c"
        "nix-store"
        "nix-store-c"
        "nix-util"
        "nix-util-c"
      ];
    };
  });
  devdoc = buildEnv {
    name = "nix-${nix-cli.version}-devdoc";
    paths = [
      nix-internal-api-docs
      nix-external-api-docs
    ];
  };

in
>>>>>>> 4eecf3c20 (Add nix-flake-c, nix_flake_init_global, nix_flake_settings_new)
(buildEnv {
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

    nix-manual
    nix-internal-api-docs
    nix-external-api-docs

  ] ++ lib.optionals (stdenv.buildPlatform.canExecute stdenv.hostPlatform) [
    nix-perl-bindings
  ];

  meta.mainProgram = "nix";
}).overrideAttrs (finalAttrs: prevAttrs: {
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
  passthru = prevAttrs.passthru // {
    inherit (nix-cli) version;

    /**
      These are the libraries that are part of the Nix project. They are used
      by the Nix CLI and other tools.

      If you need to use these libraries in your project, we recommend to use
      the `-c` C API libraries exclusively, if possible.

      We also recommend that you build the complete package to ensure that the unit tests pass.
      You could do this in CI, or by passing it in an unused environment variable. e.g in a `mkDerivation` call:

      ```nix
        buildInputs = [ nix.libs.nix-util-c nix.libs.nix-store-c ];
        # Make sure the nix libs we use are ok
        unusedInputsForTests = [ nix ];
        disallowedReferences = nix.all;
      ```
     */
    libs = {
      inherit
        nix-util
        nix-util-c
        nix-store
        nix-store-c
        nix-fetchers
        nix-expr
        nix-expr-c
        nix-flake
        nix-flake-c
        nix-main
        nix-main-c
      ;
    };
  };
})
