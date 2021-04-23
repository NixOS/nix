R""(

# Examples

* Find which packages in nixpkgs are cached

  ```console
  # nix weather nixpkgs --substituters https://cache.nixos.org
  Substituter https://cache.nixos.org
        96% of paths have substitutes available (93198 of 96902)
     237.5G compressed size
     670.2G uncompressed size
   ```

# Description

`nix weather` checks what paths are available in the provided
substituters. This is useful for determining what is missing from your
cache.

)""
