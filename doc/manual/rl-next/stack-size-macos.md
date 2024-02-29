---
synopsis: Stack size is increased on macOS
prs: 9860
---

Previously, Nix would set the stack size to 64MiB on Linux, but would leave the
stack size set to the default (approximately 8KiB) on macOS. Now, the stack
size is correctly set to 64MiB on macOS as well, which should reduce stack
overflow segfaults in deeply-recursive Nix expressions.
