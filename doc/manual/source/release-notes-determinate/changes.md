# Changes between Nix and Determinate Nix

This section lists the differences between upstream Nix 2.24 and Determinate Nix 3.4.0.

* In Determinate Nix, flakes are stable. You no longer need to enable the `flakes` experimental feature.

* In Determinate Nix, the new Nix CLI (i.e. the `nix` command) is stable. You no longer need to enable the `nix-command` experimental feature.

* Determinate Nix has a setting [`json-log-path`](@docroot@/command-ref/conf-file.md#conf-json-log-path) to send a copy of all Nix log messages (in JSON format) to a file or Unix domain socket.

* Determinate Nix has made `nix profile install` an alias to `nix profile add`, a more symmetrical antonym of `nix profile remove`.

* `nix-channel` and `channel:` url syntax (like `channel:nixos-24.11`) is deprecated, see: https://github.com/DeterminateSystems/nix-src/issues/34

* Using indirect flake references and implicit inputs is deprecated, see: https://github.com/DeterminateSystems/nix-src/issues/37

* Warnings around "dirty trees" are updated to reduce "dirty" jargon, and now refers to "uncommitted changes".
