---
synopsis: "Make `nix store gc` use the auto-GC policy"
prs: 7851
---

Automatic garbage collection is now only enabled if you set [`auto-gc
= true`](@docroot@/command-ref/conf-file.md#conf-auto-gc). You will
probably also want to set
[`gc-threshold`](@docroot@/command-ref/conf-file.md#conf-gc-threshold)
to configure when the garbage collector kicks in, e.g. `1G` to make it
run when free space drops below 1 gigabyte.
