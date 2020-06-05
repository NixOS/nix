with import ./lib.nix;

builtins.mapAttrs (name: value: name + "-" + value) { x = "foo"; y = "bar"; }
