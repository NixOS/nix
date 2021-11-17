# Release X.Y (202?-??-??)

* A new function `builtins.readFileType` is available. It is similar to
  `builtins.readDir` but acts on a single file or directory.

* The `builtins.readDir` function has been optimized when encountering not-yet-known
  file types from POSIX's `readdir`. In such cases the type of each file is/was
  discovered by making multiple syscalls. This change makes these operations
  lazy such that these lookups will only be performed if the attribute is used.
  This optimization affects a minority of filesystems and operating systems.

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
