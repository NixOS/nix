This section lists commands and options that you can use when you work
with Nix.


# Diagrams

## Store, substituters and garbage collector

This diagram explains what happens in the nix-store when you use nix-shell and when garbage collection happens.

```mermaid
flowchart TB
    nixshell([nix-shell]) -->|lookup| nix-store
    nixshell([nix-shell]) -->|fallback to if missing locally| nix-daemon([nix-daemon])
    nix-daemon -->|store into| nix-store[(nixstore)]
    nix-daemon --->|build it if not available in a substituter| substituters([substituters server i.e. cache.nixos.org])
    nix-store -->|derivations can be linked to prevent GC| .links[(.links)]
     
    nix-collect-garbage([nix-collect-garbage]) -->|clean derivation not registered in .links| nix-store
```

## Nix-build


The following diagram explains how `nix-build` is a wrapper around `nix-instantiate` + `nix-store --realise`.

```mermaid
flowchart TB

    hello.nix -->|nix-instantiate| drv
    drv(/nix/store/somelonghashhere-hello.drv)
    drv -->|nix-store --realise| drv2(/nix/store/anotherlonghashere-hello)
    hello.nix -->|nix-build| drv2
```
