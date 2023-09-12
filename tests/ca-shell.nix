{ inNixShell ? false, ... }@args: import ./shell.nix (args // { contentAddressed = true; })
