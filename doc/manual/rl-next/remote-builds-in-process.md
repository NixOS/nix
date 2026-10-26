---
synopsis: "Builds on `ssh-ng` machines no longer start a build hook process"
---

With the default [`build-hook`](@docroot@/command-ref/conf-file.md#conf-build-hook), Nix now builds on [`ssh-ng`](@docroot@/store/types/ssh-store.md) machines itself instead of starting a `nix __build-remote` process for each build.
The daemon (or the client, when there is no daemon) drives all its remote builds from one process and keeps running its other builds meanwhile.
Other machine types (`ssh://`, local stores) and custom `build-hook` programs still go through the hook, with the same protocol, and Nix no longer blocks while it waits for the hook's reply.

For `ssh-ng` machines:

- A failed remote build keeps the remote's failure status, so `nix-build` exits with 100, 101 or 102 as for a local failure, instead of 1.
- A machine that cannot be reached is skipped for the rest of that build; later builds try it again.
- Each build opens its own `ssh` connection; connection sharing (`ControlMaster`) is not used.
- `ssh` runs with `-oBatchMode=yes` and fails instead of prompting, so a builder's key must work without a passphrase prompt (for example through an agent).
