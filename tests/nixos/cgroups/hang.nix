{ }:

with import <nixpkgs> { };

runCommand "hang"
  {
    requiredSystemFeatures = "uid-range";
  }
  ''
    sleep infinity
  ''
