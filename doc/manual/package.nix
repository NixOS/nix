{
  lib,
  mkMesonDerivation,

  meson,
  ninja,
  lowdown-unsandboxed,
  mdbook,
  mdbook-linkcheck,
  jq,
  python3,
  rsync,
  nixComponents,

  # Configuration Options

  version,
}:

let
  inherit (lib) fileset;
  inherit
    (nixComponents.overrideScope (
      # HACK: nix-manual depends on nix-cli, which depends on nix-store, which
      # requires nixManDir to point to nix man pages (which get built in this derivation).
      # To break the recursion we build a dummy nix version, which has nixManDir set
      # to a broken value equal to the mandir output of the nix-store derivation.
      # NOTE: Runtime closure does NOT depend on any of the "bootstrap" nix components.
      # TODO: Fix this mess. Path to the manual has to be configured at runtime
      # (maybe with a wrapper?) rather than compile-time.
      final: prev: { nix-store = prev.nix-store.override { nixManDir = ""; }; }
    ))
    nix-cli
    ;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-manual";
  inherit version;

  workDir = ./.;
  fileset =
    fileset.difference
      (fileset.unions [
        ../../.version
        # Too many different types of files to filter for now
        ../../doc/manual
        ./.
      ])
      # Do a blacklist instead
      ../../doc/manual/package.nix;

  # TODO the man pages should probably be separate
  outputs = [
    "out"
    "man"
  ];

  # Hack for sake of the dev shell
  passthru.externalNativeBuildInputs = [
    meson
    ninja
    (lib.getBin lowdown-unsandboxed)
    mdbook
    mdbook-linkcheck
    jq
    python3
    rsync
  ];

  nativeBuildInputs = finalAttrs.passthru.externalNativeBuildInputs ++ [
    nix-cli
  ];

  preConfigure = ''
    chmod u+w ./.version
    echo ${finalAttrs.version} > ./.version
  '';

  postInstall = ''
    mkdir -p ''$out/nix-support
    echo "doc manual ''$out/share/doc/nix/manual" >> ''$out/nix-support/hydra-build-products
  '';

  meta = {
    platforms = lib.platforms.all;
  };
})
