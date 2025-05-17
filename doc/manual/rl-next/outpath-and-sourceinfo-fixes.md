---
synopsis: Non-flake inputs now contain a `sourceInfo` attribute
issues: 13164
prs: 13170
---

Flakes have always a `sourceInfo` attribute which describes the source of the flake.
The `sourceInfo.outPath` is often identical to the flake's `outPath`, however it can differ when the flake is located in a subdirectory of its source.

Non-flake inputs (i.e. inputs with `flake = false`) can also be located at some path _within_ a wider source.
This usually happens when defining a relative path input within the same source as the parent flake, e.g. `inputs.foo.url = ./some-file.nix`.
Such relative inputs will now inherit their parent's `sourceInfo`.

This also means it is now possible to use `?dir=subdir` on non-flake inputs.

This iterates on the work done in 2.26 to improve relative path support ([#10089](https://github.com/NixOS/nix/pull/10089)),
and resolves a regression introduced in 2.28 relating to nested relative path inputs ([#13164](https://github.com/NixOS/nix/issues/13164)).
