builtins.glob "./dir?" == [ ./dir1 ./dir2 ./dir3 ./dir4 ] &&
builtins.glob "./eval-*o*y*-glob.nix" == [ ./eval-okay-glob.nix ] &&
builtins.glob "./eval-okay-glob.[!nix][!out]*" == [ ./eval-okay-glob.exp ]
