(import <nixpkgs/nixos/lib/eval-config.nix> {
  system = "x86_64-linux";
  modules = [ ./configuration.nix ];
}).config.system.build.toplevel.drvPath
