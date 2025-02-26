# Release 2.27.0 (2025-03-03)

- `inputs.self.submodules` flake attribute [#12421](https://github.com/NixOS/nix/pull/12421)

  Flakes in Git repositories can now declare that they need Git submodules to be enabled:
  ```
  {
    inputs.self.submodules = true;
  }
  ```
  Thus, it's no longer needed for the caller of the flake to pass `submodules = true`.

- Git LFS support [#10153](https://github.com/NixOS/nix/pull/10153) [#12468](https://github.com/NixOS/nix/pull/12468)

  The Git fetcher now supports Large File Storage (LFS). This can be enabled by passing the attribute `lfs = true` to the fetcher, e.g.
  ```console
  nix flake prefetch 'git+ssh://git@github.com/Apress/repo-with-large-file-storage.git?lfs=1'
  ```

  A flake can also declare that it requires LFS to be enabled:
  ```
  {
    inputs.self.lfs = true;
  }
  ```

  Author: [**@b-camacho**](https://github.com/b-camacho), [**@kip93**](https://github.com/kip93)

- Set `FD_CLOEXEC` on sockets created by curl [#12439](https://github.com/NixOS/nix/pull/12439)

  Curl created sockets without setting `FD_CLOEXEC`/`SOCK_CLOEXEC`. This could previously cause connections to remain open forever when using commands like `nix shell`. This change sets the `FD_CLOEXEC` flag using a `CURLOPT_SOCKOPTFUNCTION` callback.

# Contributors

Querying GitHub API for 5cf9e18167b86f39864e39e5fe129e5f6c1a15e0, to get handle for fabianm88@gmail.com
