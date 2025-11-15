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
    ../../src/libutil-tests/data/hash
    ../../src/libstore-tests/data/content-address
    ../../src/libstore-tests/data/store-path
    ../../src/libstore-tests/data/realisation
    ../../src/libstore-tests/data/derivation
    ../../src/libstore-tests/data/derived-path
    ../../src/libstore-tests/data/path-info
    ../../src/libstore-tests/data/nar-info
    ../../src/libstore-tests/data/build-result
    ./.
  ];

  outputs = [ "out" ];

  nativeBuildInputs = [
    meson
    ninja
    jsonschema
  ];

  doCheck = true;

  mesonCheckFlags = [ "--print-errorlogs" ];

  postInstall = ''
    touch $out
  '';

  meta = {
    platforms = lib.platforms.all;
  };
})
