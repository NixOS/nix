# Release 2.14 (2023-02-28)

* A new function `builtins.readFileType` is available. It is similar to
  `builtins.readDir` but acts on a single file or directory.

* In flakes, the `.outPath` attribute of a flake now always refers to
  the directory containing the `flake.nix`. This was not the case for
  when `flake.nix` was in a subdirectory of e.g. a Git repository.
  The root of the source of a flake in a subdirectory is still
  available in `.sourceInfo.outPath`.

* In derivations that use structured attributes, you can now use `unsafeDiscardReferences`
  to disable scanning a given output for runtime dependencies:
  ```nix
  __structuredAttrs = true;
  unsafeDiscardReferences.out = true;
  ```
  This is useful e.g. when generating self-contained filesystem images with
  their own embedded Nix store: hashes found inside such an image refer
  to the embedded store and not to the host's Nix store.

  This requires the `discard-references` experimental feature.
