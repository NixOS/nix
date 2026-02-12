---
synopsis: "Improved structured logging for remote builds"
---

Several improvements to structured log output when building on remote machines
(via `--builders` or `--store ssh-ng://`). These primarily benefit tools that
consume `--log-format internal-json` output, such as `nix-output-monitor`:

- Progress indicators for remote builds are now more accurate. The expected
  build count updates incrementally as the remote daemon discovers
  sub-dependencies, rather than only reflecting the top-level derivation.

- Build duration reporting is more accurate. The input upload phase is now
  distinguished from the actual build, so time spent copying dependencies to the
  remote builder is no longer counted as build time.

- Remote and local activities can now be distinguished. All log events
  forwarded from a remote daemon carry a `"machine"` field with the remote
  store URI (e.g. `"ssh-ng://host"`) in the JSON log output. The progress bar
  also fills in the machine name for remote builds that lack one.

- Remote activity IDs are now namespaced to prevent collisions with local
  activity IDs.
