R""(

# Examples

With [nixpkgs-fmt](https://github.com/nix-community/nixpkgs-fmt):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.nixpkgs-fmt;
  };
}
```

- Format the current flake: `$ nix fmt`

- Format a specific folder or file: `$ nix fmt ./folder ./file.nix`

With [nixfmt](https://github.com/serokell/nixfmt):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.nixfmt;
  };
}
```

- Format specific files: `$ nix fmt ./file1.nix ./file2.nix`

With [Alejandra](https://github.com/kamadorueda/alejandra):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.alejandra;
  };
}
```

- Format the current flake: `$ nix fmt`

- Format a specific folder or file: `$ nix fmt ./folder ./file.nix`

# Description

`nix fmt` will rewrite all Nix files (\*.nix) to a canonical format
using the formatter specified in your flake.

)""
