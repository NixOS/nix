builtins.pathExists (builtins.toPath ./lib.nix)
&& builtins.pathExists (builtins.toPath (builtins.toString ./lib.nix))
&& !builtins.pathExists (builtins.toPath (builtins.toString ./bla.nix))
&& builtins.pathExists ./lib.nix
&& !builtins.pathExists ./bla.nix
