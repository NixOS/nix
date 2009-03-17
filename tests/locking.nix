with import ./config.nix;

let

  mkDrv = text: inputs: mkDerivation {
    name = "locking";
    builder = ./locking.builder.sh;
    inherit text inputs;
  };

  a = mkDrv "a" [];
  b = mkDrv "b" [a];
  c = mkDrv "c" [a b];
  d = mkDrv "d" [a];
  e = mkDrv "e" [c d];

in e
