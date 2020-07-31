# Common Environment Variables

Most Nix commands interpret the following environment variables:

  - `IN_NIX_SHELL`  
    Indicator that tells if the current environment was set up by
    `nix-shell`. Since Nix 2.0 the values are `"pure"` and `"impure"`

  - `NIX_PATH`  
    A colon-separated list of directories used to look up Nix
    expressions enclosed in angle brackets (i.e., `<path>`). For
    instance, the value
    
        /home/eelco/Dev:/etc/nixos
    
    will cause Nix to look for paths relative to `/home/eelco/Dev` and
    `/etc/nixos`, in this order. It is also possible to match paths
    against a prefix. For example, the value
    
        nixpkgs=/home/eelco/Dev/nixpkgs-branch:/etc/nixos
    
    will cause Nix to search for `<nixpkgs/path>` in
    `/home/eelco/Dev/nixpkgs-branch/path` and `/etc/nixos/nixpkgs/path`.
    
    If a path in the Nix search path starts with `http://` or
    `https://`, it is interpreted as the URL of a tarball that will be
    downloaded and unpacked to a temporary location. The tarball must
    consist of a single top-level directory. For example, setting
    `NIX_PATH` to
    
        nixpkgs=https://github.com/NixOS/nixpkgs/archive/nixos-15.09.tar.gz
    
    tells Nix to download the latest revision in the Nixpkgs/NixOS 15.09
    channel.
    
    A following shorthand can be used to refer to the official channels:
    
        nixpkgs=channel:nixos-15.09
    
    The search path can be extended using the `-I` option, which takes
    precedence over `NIX_PATH`.

  - `NIX_IGNORE_SYMLINK_STORE`  
    Normally, the Nix store directory (typically `/nix/store`) is not
    allowed to contain any symlink components. This is to prevent
    “impure” builds. Builders sometimes “canonicalise” paths by
    resolving all symlink components. Thus, builds on different machines
    (with `/nix/store` resolving to different locations) could yield
    different results. This is generally not a problem, except when
    builds are deployed to machines where `/nix/store` resolves
    differently. If you are sure that you’re not going to do that, you
    can set `NIX_IGNORE_SYMLINK_STORE` to `1`.
    
    Note that if you’re symlinking the Nix store so that you can put it
    on another file system than the root file system, on Linux you’re
    better off using `bind` mount points, e.g.,

    ```console
    $ mkdir /nix
    $ mount -o bind /mnt/otherdisk/nix /nix
    ```
    
    Consult the mount 8 manual page for details.

  - `NIX_STORE_DIR`  
    Overrides the location of the Nix store (default `prefix/store`).

  - `NIX_DATA_DIR`  
    Overrides the location of the Nix static data directory (default
    `prefix/share`).

  - `NIX_LOG_DIR`  
    Overrides the location of the Nix log directory (default
    `prefix/var/log/nix`).

  - `NIX_STATE_DIR`  
    Overrides the location of the Nix state directory (default
    `prefix/var/nix`).

  - `NIX_CONF_DIR`  
    Overrides the location of the system Nix configuration directory
    (default `prefix/etc/nix`).

  - `NIX_USER_CONF_FILES`  
    Overrides the location of the user Nix configuration files to load
    from (defaults to the XDG spec locations). The variable is treated
    as a list separated by the `:` token.

  - `TMPDIR`  
    Use the specified directory to store temporary files. In particular,
    this includes temporary build directories; these can take up
    substantial amounts of disk space. The default is `/tmp`.

  - `NIX_REMOTE`  
    This variable should be set to `daemon` if you want to use the Nix
    daemon to execute Nix operations. This is necessary in [multi-user
    Nix installations](../installation/multi-user.md). If the Nix
    daemon's Unix socket is at some non-standard path, this variable
    should be set to `unix://path/to/socket`. Otherwise, it should be
    left unset.

  - `NIX_SHOW_STATS`  
    If set to `1`, Nix will print some evaluation statistics, such as
    the number of values allocated.

  - `NIX_COUNT_CALLS`  
    If set to `1`, Nix will print how often functions were called during
    Nix expression evaluation. This is useful for profiling your Nix
    expressions.

  - `GC_INITIAL_HEAP_SIZE`  
    If Nix has been configured to use the Boehm garbage collector, this
    variable sets the initial size of the heap in bytes. It defaults to
    384 MiB. Setting it to a low value reduces memory consumption, but
    will increase runtime due to the overhead of garbage collection.
