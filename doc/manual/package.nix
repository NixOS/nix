{
  lib,
  callPackage,
  mkMesonDerivation,
  runCommand,

  meson,
  ninja,
  lowdown-unsandboxed,
  mdbook,
  jq,
  python3,
  rsync,
  nix-cli,
  changelog-d,
  json-schema-for-humans,
  officialRelease,

  # Configuration Options

  version,

  # `tests` attribute
  testers,
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
        # For example JSON
        ../../src/libutil-tests/data/memory-source-accessor
        ../../src/libutil-tests/data/hash
        ../../src/libstore-tests/data/content-address
        ../../src/libstore-tests/data/store-path
        ../../src/libstore-tests/data/realisation
        ../../src/libstore-tests/data/derivation
        ../../src/libstore-tests/data/derived-path
        ../../src/libstore-tests/data/path-info
        ../../src/libstore-tests/data/nar-info
        ../../src/libstore-tests/data/build-result
        ../../src/libstore-tests/data/dummy-store
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

  nativeBuildInputs = [
    nix-cli
    meson
    ninja
    (lib.getBin lowdown-unsandboxed)
    mdbook
    jq
    python3
    rsync
    json-schema-for-humans
    changelog-d
  ]
  ++ lib.optionals (!officialRelease) [
    # When not an official release, we likely have changelog entries that have
    # yet to be rendered.
    # When released, these are rendered into a committed file to save a dependency.
    changelog-d
  ];

  mesonFlags = [
    (lib.mesonBool "official-release" officialRelease)
  ];

  preConfigure = ''
    chmod u+w ./.version
    echo ${finalAttrs.version} > ./.version
  '';

  postInstall = ''
    mkdir -p ''$out/nix-support
    echo "doc manual ''$out/share/doc/nix/manual" >> ''$out/nix-support/hydra-build-products
  '';

  /**
    The root of the HTML manual.
    E.g. "${nix-manual.site}/index.html" exists.
  */
  passthru.site = finalAttrs.finalPackage + "/share/doc/nix/manual";

  passthru.tests =
    let
      redirect-targets = callPackage ./redirect-targets-html.nix { };
    in
    {
      # https://nixos.org/manual/nixpkgs/stable/index.html#tester-lycheeLinkCheck
      linkcheck = testers.lycheeLinkCheck {
        site =
          let
            plain = finalAttrs.finalPackage.site;
          in
          runCommand "nix-manual-with-redirect-targets" { } ''
            cp -r ${plain} $out
            chmod -R u+w $out
            cp ${redirect-targets}/redirect-targets.html $out/redirect-targets.html
          '';
        extraConfig = {
          exclude = [
            # Exclude auto-generated JSON schema documentation which has
            # auto-generated fragment IDs that don't match the link references
            ".*/protocols/json/.*\\.html"
            # Exclude undocumented builtins
            ".*/language/builtins\\.html#builtins-addErrorContext"
            ".*/language/builtins\\.html#builtins-appendContext"
          ];
        };
      };
    };

  meta = {
    platforms = lib.platforms.all;
  };
})
