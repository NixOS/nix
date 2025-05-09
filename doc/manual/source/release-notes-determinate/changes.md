# Changes between Nix and Determinate Nix

This section lists the differences between upstream Nix 2.28 and Determinate Nix 3.5.1.<!-- differences -->

* In Determinate Nix, flakes are stable. You no longer need to enable the `flakes` experimental feature.

* In Determinate Nix, the new Nix CLI (i.e. the `nix` command) is stable. You no longer need to enable the `nix-command` experimental feature.

* Determinate Nix has a setting [`json-log-path`](@docroot@/command-ref/conf-file.md#conf-json-log-path) to send a copy of all Nix log messages (in JSON format) to a file or Unix domain socket.

* Determinate Nix has made `nix profile install` an alias to `nix profile add`, a more symmetrical antonym of `nix profile remove`.

* `nix-channel` and `channel:` url syntax (like `channel:nixos-24.11`) is deprecated, see: https://github.com/DeterminateSystems/nix-src/issues/34

* Using indirect flake references and implicit inputs is deprecated, see: https://github.com/DeterminateSystems/nix-src/issues/37

* Warnings around "dirty trees" are updated to reduce "dirty" jargon, and now refers to "uncommitted changes".

<!-- Determinate Nix version 3.4.2 -->

<!-- Determinate Nix version 3.5.0 -->

<!-- Determinate Nix version 3.5.1 -->

* Deprecate upgrade-nix command by @gustavderdrache in [DeterminateSystems/nix-src#55](https://github.com/DeterminateSystems/nix-src/pull/55)

* Lazy trees v2 by @edolstra in [DeterminateSystems/nix-src#27](https://github.com/DeterminateSystems/nix-src/pull/27)

* Improve lazy trees backward compatibility by @edolstra in [DeterminateSystems/nix-src#56](https://github.com/DeterminateSystems/nix-src/pull/56)

* Canonicalize flake input URLs before checking flake.lock file staleness, for dealing with `dir` in URL-style flakerefs by @edolstra in [DeterminateSystems/nix-src#57](https://github.com/DeterminateSystems/nix-src/pull/57)

* Improve build failure error messages by @edolstra in [DeterminateSystems/nix-src#58](https://github.com/DeterminateSystems/nix-src/pull/58)
