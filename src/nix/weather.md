R""(

# Examples

* Find which packages in nixpkgs are cached

  ```console
  # nix weather nixpkgs --substituters https://cache.nixos.org
  Substituter https://cache.nixos.org
        96% of paths have substitutes available (93198 of 96902)
     670.2G of nars (compressed)
     237.5G on disk (uncompressed)
   ```

# Description

`nix weather` checks what paths are available in the provided
substituters. This is useful for determining what is missing from your
cache.

)""
