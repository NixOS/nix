# Release 0.16 (2010-08-17)

This release has the following improvements:

  - The Nix expression evaluator is now much faster in most cases:
    typically, [3 to 8 times compared to the old
    implementation](http://www.mail-archive.com/nix-dev@cs.uu.nl/msg04113.html).
    It also uses less memory. It no longer depends on the ATerm library.

  - Support for configurable parallelism inside builders. Build scripts
    have always had the ability to perform multiple build actions in
    parallel (for instance, by running `make -j
                            2`), but this was not desirable because the number of actions to be
    performed in parallel was not configurable. Nix now has an option
    `--cores
                            N` as well as a configuration setting `build-cores =
                            N` that causes the environment variable `NIX_BUILD_CORES` to be set
    to *N* when the builder is invoked. The builder can use this at its
    discretion to perform a parallel build, e.g., by calling `make -j
                            N`. In Nixpkgs, this can be enabled on a per-package basis by
    setting the derivation attribute `enableParallelBuilding` to `true`.

  - `nix-store -q` now supports XML output through the `--xml` flag.

  - Several bug fixes.
