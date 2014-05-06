builtins.importWithSettings {
  nix-path = [ { this = ./test-nix-path.nix; } ];
} ./test-nix-path.nix
