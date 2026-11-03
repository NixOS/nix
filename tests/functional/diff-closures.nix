with import ./config.nix;

let
  makePackage =
    name: contents:
    mkDerivation {
      inherit name;
      buildCommand = ''
        mkdir "$out"
        printf '%s' '${contents}' > "$out/data"
      '';
    };

  version1 = makePackage "foo-1.0" "version 1";
  version2 = makePackage "foo-2.0" "version 2";

  small = makePackage "sized-1.0" "small";
  large = makePackage "sized-1.0" (
    builtins.concatStringsSep "" (builtins.genList (_: "0123456789") 2048)
  );

  makeClosure =
    version: sized:
    mkDerivation {
      name = "test-closure";
      buildCommand = ''
        mkdir "$out"
        printf '%s\n%s\n' '${version}' '${sized}' > "$out/refs"
      '';
    };
in
{
  before = makeClosure version1 small;
  after = makeClosure version2 large;
}
