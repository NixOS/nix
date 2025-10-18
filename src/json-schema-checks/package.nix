# Run with: nix build .#nix-json-schema-checks
{
  lib,
  mkMesonDerivation,

  meson,
  ninja,
  jsonschema,

  # Configuration Options

  version,
}:

mkMesonDerivation (finalAttrs: {
  pname = "nix-json-schema-checks";
  inherit version;

  workDir = ./.;
  fileset = lib.fileset.unions [
    ../../.version
    ../../doc/manual/source/protocols/json/schema
    ../../src/libstore-tests/data/derivation
    ./.
  ];

  outputs = [ "out" ];

  passthru.externalNativeBuildInputs = [
    jsonschema
  ];

  nativeBuildInputs = [
    meson
    ninja
  ]
  ++ finalAttrs.passthru.externalNativeBuildInputs;

  doCheck = true;

  mesonCheckFlags = [ "--print-errorlogs" ];

  postInstall = ''
    touch $out
  '';

  meta = {
    platforms = lib.platforms.all;
  };
})
