R""(

# Examples

* Show the output attributes provided by the `patchelf` flake:

  ```console
  github:NixOS/patchelf/f34751b88bd07d7f44f5cd3200fb4122bf916c7e
  ├───checks
  │   └───{x86_64-linux,aarch64-linux,i686-linux}
  │       └───build: derivation 'patchelf-0.12.20201207.f34751b'
  ├───packages
  │   └───{x86_64-linux,aarch64-linux,i686-linux}
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

  Systems in the `apps`, `checks`, `devShells`, and `packages` categories are folded by default to reduce visual clutter. The local system is highlighted in bold brackets `[x86_64-linux]` while other systems are shown faintly without brackets.

* Show all systems separately (no folding):

  ```console
  github:NixOS/patchelf/f34751b88bd07d7f44f5cd3200fb4122bf916c7e
  ├───checks
  │   ├───x86_64-linux
  │   │   └───build: derivation 'patchelf-0.12.20201207.f34751b'
  │   ├───aarch64-linux
  │   │   └───build: derivation 'patchelf-0.12.20201207.f34751b'
  │   └───i686-linux
  │       └───build: derivation 'patchelf-0.12.20201207.f34751b'
  └───...
  ```

  Use `--no-system-folding` to disable system folding and show all systems separately.

# Description

This command shows the output attributes provided by the flake
specified by flake reference *flake-url*. These are the top-level
attributes in the `outputs` of the flake, as well as lower-level
attributes for some standard outputs (e.g. `packages` or `checks`).

By default, system architectures in the `apps`, `checks`, `devShells`, and `packages` categories are folded into a single display node to reduce visual clutter. The local system is highlighted in bold with brackets (e.g., `[x86_64-linux]`), while other systems are shown faintly without brackets (e.g., `aarch64-linux`). Systems are sorted in reverse alphabetical order (x86_64 before aarch64).

System folding can be disabled with `--no-system-folding` or automatically disabled when using `--json` or `--all-systems` flags.

With `--json`, the output is in a JSON representation suitable for automatic
processing by other tools. System folding is automatically disabled in JSON mode.

)""
