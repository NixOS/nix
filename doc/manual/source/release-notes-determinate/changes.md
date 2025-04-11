# Changes between Nix and Determinate Nix

This section lists the differences between upstream Nix 2.24 and Determinate Nix 3.3.0.

* In Determinate Nix, flakes are stable. You no longer need to enable the `flakes` experimental feature.

* In Determinate Nix, the new Nix CLI (i.e. the `nix` command) is stable. You no longer need to enable the `nix-command` experimental feature.

* Determinate Nix has a setting [`json-log-path`](@docroot@/command-ref/conf-file.md#conf-json-log-path) to send a copy of all Nix log messages (in JSON format) to a file or Unix domain socket.

* Determinate Nix has made `nix profile install` an alias to `nix profile add`, a more symmetrical antonym of `nix profile remove`.
