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
  nix-cli,
  changelog-d,
  officialRelease,

  # Configuration Options

  version,
}:

let
  inherit (lib) fileset;
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
    changelog-d
  ]
  ++ lib.optionals (!officialRelease) [
    # When not an official release, we likely have changelog entries that have
    # yet to be rendered.
    # When released, these are rendered into a committed file to save a dependency.
    changelog-d
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
