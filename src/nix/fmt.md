R""(

# Description

`nix fmt` calls the formatter specified in the flake.

Flags can be forwarded to the formatter by using `--` followed by the flags.

Any arguments will be forwarded to the formatter. Typically these are the files to format.


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

With [nixfmt](https://github.com/NixOS/nixfmt):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.nixfmt-rfc-style;
  };
}
```

With [Alejandra](https://github.com/kamadorueda/alejandra):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.alejandra;
  };
}
```

)""
