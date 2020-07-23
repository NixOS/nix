# Release 1.2 (2012-12-06)

This release has the following improvements and changes:

  - Nix has a new binary substituter mechanism: the *binary cache*. A
    binary cache contains pre-built binaries of Nix packages. Whenever
    Nix wants to build a missing Nix store path, it will check a set of
    binary caches to see if any of them has a pre-built binary of that
    path. The configuration setting `binary-caches` contains a list of
    URLs of binary caches. For instance, doing
    
        $ nix-env -i thunderbird --option binary-caches http://cache.nixos.org
    
    will install Thunderbird and its dependencies, using the available
    pre-built binaries in <http://cache.nixos.org>. The main advantage
    over the old “manifest”-based method of getting pre-built binaries
    is that you don’t have to worry about your manifest being in sync
    with the Nix expressions you’re installing from; i.e., you don’t
    need to run `nix-pull` to update your manifest. It’s also more
    scalable because you don’t need to redownload a giant manifest file
    every time.
    
    A Nix channel can provide a binary cache URL that will be used
    automatically if you subscribe to that channel. If you use the
    Nixpkgs or NixOS channels (<http://nixos.org/channels>) you
    automatically get the cache <http://cache.nixos.org>.
    
    Binary caches are created using `nix-push`. For details on the
    operation and format of binary caches, see the `nix-push` manpage.
    More details are provided in [this nix-dev
    posting](https://nixos.org/nix-dev/2012-September/009826.html).

  - Multiple output support should now be usable. A derivation can
    declare that it wants to produce multiple store paths by saying
    something like
    
        outputs = [ "lib" "headers" "doc" ];
    
    This will cause Nix to pass the intended store path of each output
    to the builder through the environment variables `lib`, `headers`
    and `doc`. Other packages can refer to a specific output by
    referring to `pkg.output`, e.g.
    
        buildInputs = [ pkg.lib pkg.headers ];
    
    If you install a package with multiple outputs using `nix-env`, each
    output path will be symlinked into the user environment.

  - Dashes are now valid as part of identifiers and attribute names.

  - The new operation `nix-store --repair-path` allows corrupted or
    missing store paths to be repaired by redownloading them. `nix-store
    --verify --check-contents
                    --repair` will scan and repair all paths in the Nix store.
    Similarly, `nix-env`, `nix-build`, `nix-instantiate` and `nix-store
    --realise` have a `--repair` flag to detect and fix bad paths by
    rebuilding or redownloading them.

  - Nix no longer sets the immutable bit on files in the Nix store.
    Instead, the recommended way to guard the Nix store against
    accidental modification on Linux is to make it a read-only bind
    mount, like this:
    
        $ mount --bind /nix/store /nix/store
        $ mount -o remount,ro,bind /nix/store
    
    Nix will automatically make `/nix/store` writable as needed (using a
    private mount namespace) to allow modifications.

  - Store optimisation (replacing identical files in the store with hard
    links) can now be done automatically every time a path is added to
    the store. This is enabled by setting the configuration option
    `auto-optimise-store` to `true` (disabled by default).

  - Nix now supports `xz` compression for NARs in addition to `bzip2`.
    It compresses about 30% better on typical archives and decompresses
    about twice as fast.

  - Basic Nix expression evaluation profiling: setting the environment
    variable `NIX_COUNT_CALLS` to `1` will cause Nix to print how many
    times each primop or function was executed.

  - New primops: `concatLists`, `elem`, `elemAt` and `filter`.

  - The command `nix-copy-closure` has a new flag `--use-substitutes`
    (`-s`) to download missing paths on the target machine using the
    substitute mechanism.

  - The command `nix-worker` has been renamed to `nix-daemon`. Support
    for running the Nix worker in “slave” mode has been removed.

  - The `--help` flag of every Nix command now invokes `man`.

  - Chroot builds are now supported on systemd machines.

This release has contributions from Eelco Dolstra, Florian Friesdorf,
Mats Erik Andersson and Shea Levy.
