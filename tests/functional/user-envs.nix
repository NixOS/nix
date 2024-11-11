# Some dummy arguments...
{ foo ? "foo"
}:

with import ./config.nix;

assert foo == "foo";

let

  platforms = let x = "foobar"; in [ x x ];

  makeDrv = name: progName: (mkDerivation {
    name = assert progName != "fail"; name;
    inherit progName system;
    builder = ./user-envs.builder.sh;
  } // {
    meta = {
      description = "A silly test package with some \${escaped anti-quotation} in it";
      inherit platforms;
    };
  });

in

  [
    (makeDrv "foo-1.0" "foo")
    (makeDrv "foo-2.0pre1" "foo")
    (makeDrv "bar-0.1" "bar")
    (makeDrv "foo-2.0" "foo")
    (makeDrv "bar-0.1.1" "bar")
    (makeDrv "foo-0.1" "foo" // { meta.priority = 10; })
    (makeDrv "fail-0.1" "fail")
  ]
