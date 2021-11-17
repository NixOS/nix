with import ./lib.nix;

concat (map (x: x + "bar") [ "foo" "bla" "xyzzy" ])