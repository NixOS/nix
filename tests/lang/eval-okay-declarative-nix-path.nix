builtins.importWithSettings {
  nix-path = [ { prefix = "this"; value = ./test-nix-path.nix; } ];
} ./test-nix-path.nix
