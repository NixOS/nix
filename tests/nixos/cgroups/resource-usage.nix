{ }:

with import <nixpkgs> { };

runCommand "resource-usage"
  {
    requiredSystemFeatures = "uid-range";
  }
  ''
    ballast=$(head -c 67108864 /dev/zero | tr "\0" "x")
    echo ''${#ballast} > $out
  ''
