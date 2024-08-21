{sleepTime ? 3}:

with import ./config.nix;

let

  mkDrv = text: inputs: mkDerivation {
    name = "parallel";
    builder = ./parallel.builder.sh;
    inherit text inputs shared sleepTime;
  };

  a = mkDrv "a" [];
  b = mkDrv "b" [a];
  c = mkDrv "c" [a];
  d = mkDrv "d" [a];
  e = mkDrv "e" [b c d];

in e
