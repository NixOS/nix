R""(

# Examples

With [nixfmt](https://github.com/NixOS/nixfmt):

- Format the current flake: `$ nix fmt`

- Format a specific folder or file: `$ nix fmt ./folder ./file.nix`

- Format specific files: `$ nix fmt ./file1.nix ./file2.nix`

## **disabled** Overriding the formatter used
```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.nixfmt-rfc-style;
  };
}
```


# Description

`nix fmt` will rewrite all Nix files (\*.nix) to a canonical format
using the formatter specified in your flake.

)""
