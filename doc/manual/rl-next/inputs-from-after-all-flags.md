---
synopsis: "`--inputs-from` takes effect after all other flags"
prs: [16857]
---

`--inputs-from` used to lock the given flake as soon as it was parsed.
Flags given after it, such as `--eval-store`, `--store`, `--include`, `--repair` or `--debugger`, had no effect on that lock or on the rest of the command, and an eval store whose type a plugin provides could not be opened at all.
It now locks the flake once all flags are parsed and the plugins are loaded.

As a consequence, `--override-flake` now takes precedence over `--inputs-from` for the same flake name, whatever their order on the command line.
Previously, whichever of the two came first won.
