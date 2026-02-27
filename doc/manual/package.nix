{
  lib,
  stdenv,
  callPackage,
  mkMesonDerivation,
  runCommand,

  meson,
  ninja,
  lowdown-unsandboxed,
  mdbook,
  jq,
  python3,
  nix-cli,
  changelog-d,
  json-schema-for-humans,
  officialRelease,

  # Configuration Options

  version,
  /**
    Whether to build the HTML manual.
    When false, only manpages are built, avoiding the mdbook dependency.
  */
  buildHtmlManual ? true,

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
        # For derivation examples referenced by symlinks in doc/manual/source/protocols/json/schema/
        ../../tests/functional/derivation
        # Too many different types of files to filter for now
        ../../doc/manual
        ./.
      ])
      # Do a blacklist instead
      ../../doc/manual/package.nix;

  # TODO the man pages should probably be separate
  outputs =
    if buildHtmlManual then
      [
        "out"
        "man"
      ]
    else
      [ "out" ]; # Only one output when HTML manual is disabled; use "out" for manpages

  # When HTML manual is disabled, install manpages to "out" instead of "man"
  mesonFlags = [
    (lib.mesonBool "official-release" officialRelease)
    (lib.mesonBool "html-manual" buildHtmlManual)
  ]
  ++ lib.optionals (!buildHtmlManual) [
    "--mandir=${placeholder "out"}/share/man"
  ];

  nativeBuildInputs = [
    nix-cli
    meson
    ninja
    (lib.getBin lowdown-unsandboxed)
    jq
    python3
  ]
  ++ lib.optionals buildHtmlManual [
    mdbook
    json-schema-for-humans
  ]
  ++ lib.optionals (!officialRelease && buildHtmlManual && !stdenv.hostPlatform.isi686) [
    # When not an official release, we likely have changelog entries that have
    # yet to be rendered.
    # When released, these are rendered into a committed file to save a dependency.
    # Broken on i686.
    changelog-d
  ];

  preConfigure = ''
    chmod u+w ./.version
    echo ${finalAttrs.version} > ./.version
  '';

  postInstall = lib.optionalString buildHtmlManual ''
    mkdir -p ''$out/nix-support
    echo "doc manual ''$out/share/doc/nix/manual" >> ''$out/nix-support/hydra-build-products
  '';

  passthru = lib.optionalAttrs buildHtmlManual {
    /**
      The root of the HTML manual.
      E.g. "${nix-manual.site}/index.html" exists.
    */

    site = finalAttrs.finalPackage + "/share/doc/nix/manual";

    tests =
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
  };

  meta = {
    platforms = lib.platforms.all;
  };
})
