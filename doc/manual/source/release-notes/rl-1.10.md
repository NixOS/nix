# Release 1.10 (2015-09-03)

This is primarily a bug fix release. It also has a number of new
features:

  - A number of builtin functions have been added to reduce
    Nixpkgs/NixOS evaluation time and memory consumption: `all`, `any`,
    `concatStringsSep`, `foldl’`, `genList`, `replaceStrings`, `sort`.

  - The garbage collector is more robust when the disk is full.

  - Nix supports a new API for building derivations that doesn’t require
    a `.drv` file to be present on disk; it only requires an in-memory
    representation of the derivation. This is used by the Hydra
    continuous build system to make remote builds more efficient.

  - The function `<nix/fetchurl.nix>` now uses a *builtin* builder (i.e.
    it doesn’t require starting an external process; the download is
    performed by Nix itself). This ensures that derivation paths don’t
    change when Nix is upgraded, and obviates the need for ugly hacks to
    support chroot execution.

  - `--version -v` now prints some configuration information, in
    particular what compile-time optional features are enabled, and the
    paths of various directories.

  - Build users have their supplementary groups set correctly.

This release has contributions from Eelco Dolstra, Guillaume Maudoux,
Iwan Aucamp, Jaka Hudoklin, Kirill Elagin, Ludovic Courtès, Manolis
Ragkousis, Nicolas B. Pierron and Shea Levy.
