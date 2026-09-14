---
synopsis: "Build hooks receive the derivation being built"
---

The [`pre-build-hook`](@docroot@/command-ref/conf-file.md#conf-pre-build-hook) and [`post-build-hook`](@docroot@/command-ref/conf-file.md#conf-post-build-hook) programs can now read the derivation in the [ATerm format](@docroot@/protocols/derivation-aterm.md) from the file descriptor whose number is given in the new `DRV_ATERM_FD` environment variable.
The post-build hook can also read a JSON object describing the build, currently with an `outputs` map from output names to store paths, from the one given in `BUILD_INFO_JSON_FD`.
The derivation is the one Nix already has in memory, so hooks can inspect what the builder runs even when it does not exist on disk as a `.drv` file, as is the case for resolved derivations arising from content-addressing derivations and also remote building with trusted users.
The new `RESOLVED_DRV_PATH` environment variable gives that derivation's store path, which may differ from `DRV_PATH`, and `DRV_NAME` gives its name, which the ATerm format does not record.
`nix derivation show` gains `--aterm-stdin`, which reads such a derivation from standard input, so a hook can obtain the JSON representation of the derivation it received without the derivation being in the store.
