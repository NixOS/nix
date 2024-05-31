---
synopsis: "`nix env shell` is the new `nix shell`, and `nix shell` remains an accepted alias"
issues: 10504
prs: 10807
---

This is part of an effort to bring more structure to the CLI subcommands.

`nix env` will be about the process environment.
Future commands may include `nix env run` and `nix env print-env`.

It is also somewhat analogous to the [planned](https://github.com/NixOS/nix/issues/10504) `nix dev shell` (currently `nix develop`), which is less about environment variables, and more about running a development shell, which is a more powerful command, but also requires more setup.
