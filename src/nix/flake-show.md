R""(

# Examples

* Show the output attributes provided by the `patchelf` flake:

  ```console
  github:NixOS/patchelf/f34751b88bd07d7f44f5cd3200fb4122bf916c7e
  ├───checks
  │   ├───aarch64-linux
  │   │   └───build: derivation 'patchelf-0.12.20201207.f34751b'
  │   ├───i686-linux
  │   │   └───build: derivation 'patchelf-0.12.20201207.f34751b'
  │   └───x86_64-linux
  │       └───build: derivation 'patchelf-0.12.20201207.f34751b'
  ├───packages
  │   ├───aarch64-linux
  │   │   └───default: package 'patchelf-0.12.20201207.f34751b'
  │   ├───i686-linux
  │   │   └───default: package 'patchelf-0.12.20201207.f34751b'
  │   └───x86_64-linux
  │       └───default: package 'patchelf-0.12.20201207.f34751b'
  ├───hydraJobs
  │   ├───build
  │   │   ├───aarch64-linux: derivation 'patchelf-0.12.20201207.f34751b'
  │   │   ├───i686-linux: derivation 'patchelf-0.12.20201207.f34751b'
  │   │   └───x86_64-linux: derivation 'patchelf-0.12.20201207.f34751b'
  │   ├───coverage: derivation 'patchelf-coverage-0.12.20201207.f34751b'
  │   ├───release: derivation 'patchelf-0.12.20201207.f34751b'
  │   └───tarball: derivation 'patchelf-tarball-0.12.20201207.f34751b'
  └───overlay: Nixpkgs overlay
  ```

# Description

This command shows the output attributes provided by the flake
specified by flake reference *flake-url*. These are the top-level
attributes in the `outputs` of the flake, as well as lower-level
attributes for some standard outputs (e.g. `packages` or `checks`).

With `--json`, the output is in a JSON representation suitable for automatic
processing by other tools.

)""
