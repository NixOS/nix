## What's Changed

### Less time "unpacking into the Git cache"

Unpacking sources into the user's cache is now takes 1/2 to 1/4 of the time it used to.
Previously, Nix serially unpacked sources into the cache.
This change takes better advantage of our users' hardware by parallelizing the import.
Real life testing shows an initial Nixpkgs import takes 3.6s on Linux, when it used to take 11.7s.

PR: [DeterminateSystems/nix-src#149](https://github.com/DeterminateSystems/nix-src/pull/149)

### Copy paths to the daemon in parallel

Determinate Nix's evaluator no longer blocks evaluation when copying paths to the store.
Previously, Nix would pause evaluation when it needed to add files to the store.
Now, the copying is performed in the background allowing evaluation to proceed.

PR: [DeterminateSystems/nix-src#162](https://github.com/DeterminateSystems/nix-src/pull/162)

### Faster Nix evaluation by reducing duplicate Nix daemon queries

Determinate Nix more effectively caches store path validity data within a single evaluation.
Previously, the Nix client would perform many thousands of exra Nix daemon requests.
Each extra request takes real time, and this change reduced a sample evaluation by over 12,000 requests.

PR: [DeterminateSystems/nix-src#157](https://github.com/DeterminateSystems/nix-src/pull/157)

### More responsive tab completion

Tab completion now implies the "--offline" flag, which disables most network requests.
Previously, tab completing Nix arguments would attempt to fetch sources and access binary caches.
Operating in offline mode improves the interactive experience of Nix when tab completing.

PR: [DeterminateSystems/nix-src#161](https://github.com/DeterminateSystems/nix-src/pull/161)

### ZFS users: we fixed the mysterious stall.

Opening the Nix database is usually instantaneous but sometimes has a several second latency.
Determinate Nix works around this issue, eliminating the frustrating random stall when running Nix commands.

PR: [DeterminateSystems/nix-src#158](https://
github.com/DeterminateSystems/nix-src/pull/158)

### Other changes

* Determinate Nix is now fully formatted by clang-format, making it easier than ever to contribute to the project.

PR: [DeterminateSystems/nix-src#159](https://github.com/DeterminateSystems/nix-src/pull/159)

* Determinate Nix is now based on upstream Nix 2.30.2.

PR: [DeterminateSystems/nix-src#160](https://github.com/DeterminateSystems/nix-src/pull/160)

* Determinate Nix now uses `main` as our development branch, moving away from `detsys-main`.

PRs:
* [DeterminateSystems/nix-src#164](https://github.com/DeterminateSystems/nix-src/pull/164)
* [DeterminateSystems/nix-src#166](https://github.com/DeterminateSystems/nix-src/pull/166)

