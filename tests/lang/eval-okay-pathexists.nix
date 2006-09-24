builtins.pathExists (builtins.toPath ./lib.nix)
&& builtins.pathExists ./lib.nix
&& !builtins.pathExists ./bla.nix
