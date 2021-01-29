# Some dummy arguments...
{ foo ? "foo"
}:

with import ./config.nix;

assert foo == "foo";

let

  makeDrv = name: progName: (mkDerivation {
    inherit name progName system;
    builder = ./user-envs.builder.sh;
  } // {
    meta = {
      description = "A silly test package with some \${escaped anti-quotation} in it";
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
  ]
