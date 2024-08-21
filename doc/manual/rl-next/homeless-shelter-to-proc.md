---
synopsis: On linux, set $HOME=/proc/homeless-shelter instead of /homeless-shelter
issues: [8313, 11295]
prs: [11300]
---

When building, $HOME is set to a non-existing directory. Previously it was always set to `/homeless-shelter`. Before a build, Nix verifies that it doesn't exist. In some scenarios (specifically when using the Linux sandbox with a single-user installation), it is possible to create the `/homeless-shelter` directory, and some tools will create it, resulting in a build error.

Now, on Linux, $HOME is set to `/proc/homeless-shelter`. This directory can never be created, since `/proc` is a virtual filesystem. This resolves the issue.
