---
synopsis: "The build hook is gone; remote builds are handled by the build scheduler itself"
issues: 1221
---

Remote builds no longer go through a helper process (`nix __build-remote`, the `build-hook` setting).
The scheduler now talks to the builders configured in [`builders`](@docroot@/command-ref/conf-file.md#conf-builders) directly:

- `ssh://` and `ssh-ng://` builders are driven over the store interface, as the helper used to do.
- Builders that are local stores (e.g. `local?root=/other/store`) are built in directly, without another scheduler in between.
- Any other kind of store as a builder is an error.

The `build-hook` and `build-hook-kill-timeout` settings have been removed.
