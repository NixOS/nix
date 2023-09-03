R""(

# Examples

* Show the inputs of the `hydra` flake:

  ```console
  # nix flake list-inputs github:NixOS/hydra
  github:NixOS/hydra/bde8d81876dfc02143e5070e42c78d8f0d83d6f7
  ├───nix: github:NixOS/nix/79aa7d95183cbe6c0d786965f0dbff414fd1aa67
  │   ├───lowdown-src: github:kristapsdz/lowdown/1705b4a26fbf065d9574dce47a94e8c7c79e052f
  │   └───nixpkgs: github:NixOS/nixpkgs/ad0d20345219790533ebe06571f82ed6b034db31
  └───nixpkgs follows input 'nix/nixpkgs'
  ```

# Description

This command shows the inputs of the flake specified by the flake
referenced *flake-url*. Since it prints the locked inputs that result
from generating or updating the lock file, this command essentially
displays the contents of the flake's lock file in human-readable form.

)""
