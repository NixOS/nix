---
synopsis: "Renamed `nixosConfigurations.`*name* to
  `configurations.`*system*`.`*name*"
prs: 10291
---

The `nixosConfigurations.`*name* output is deprecated in favor of
`configurations.`*system*`.`*name* where *system* the name of the system that
would be used to build the configuration, similar to `packages`, `checks` and
other system-dependent flake outputs that produce derivations.

The old output will continue to work, but `nix flake check` will issue a
deprecation warning about it.

It is strongly recommended to keep configurations identical when defined under
the same name with multiple systems, e.g.

* `configurations.aarch64-linux.webserver` defines a webserver configuration
  built on and for `aarch64-linux` system.
* `configurations.x86_64-linux.webserver` describes the same configuration, but
   cross-compiled from `x86_64-linux` system.

Note though that bit-wise identical cross-compilation is currently not
possible without experimental [content-addressed derivations] (and even then a
lot of effort is necessary on the Nixpkgs side to make that work).

[content-addressed derivations]: @docroot@/contributing/experimental-features.md#xp-feature-ca-derivations
