# Common Environment Variables

Most Nix commands interpret the following environment variables:

- <span id="env-IN_NIX_SHELL">[`IN_NIX_SHELL`](#env-IN_NIX_SHELL)</span>

  Indicator that tells if the current environment was set up by
  `nix-shell`. It can have the values `pure` or `impure`.

- <span id="env-NIX_PATH">[`NIX_PATH`](#env-NIX_PATH)</span>

  A colon-separated list of search path entries used to resolve [lookup paths](@docroot@/language/constructs/lookup-path.md).

  This environment variable overrides the value of the [`nix-path` configuration setting](@docroot@/command-ref/conf-file.md#conf-nix-path).

  It can be extended using the [`-I` option](@docroot@/command-ref/opt-common.md#opt-I).

  > **Example**
  >
  > ```bash
  > $ export NIX_PATH=`/home/eelco/Dev:nixos-config=/etc/nixos
  > ```

  If `NIX_PATH` is set to an empty string, resolving search paths will always fail.

  > **Example**
  >
  > ```bash
  > $ NIX_PATH= nix-instantiate --eval '<nixpkgs>'
  > error: file 'nixpkgs' was not found in the Nix search path (add it using $NIX_PATH or -I)
  > ```

- <span id="env-NIX_IGNORE_SYMLINK_STORE">[`NIX_IGNORE_SYMLINK_STORE`](#env-NIX_IGNORE_SYMLINK_STORE)</span>

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

- <span id="env-NIX_STORE_DIR">[`NIX_STORE_DIR`](#env-NIX_STORE_DIR)</span>

  Overrides the location of the Nix store (default `prefix/store`).

- <span id="env-NIX_DATA_DIR">[`NIX_DATA_DIR`](#env-NIX_DATA_DIR)</span>

  Overrides the location of the Nix static data directory (default
  `prefix/share`).

- <span id="env-NIX_LOG_DIR">[`NIX_LOG_DIR`](#env-NIX_LOG_DIR)</span>

  Overrides the location of the Nix log directory (default
  `prefix/var/log/nix`).

- <span id="env-NIX_STATE_DIR">[`NIX_STATE_DIR`](#env-NIX_STATE_DIR)</span>

  Overrides the location of the Nix state directory (default
  `prefix/var/nix`).

- <span id="env-NIX_CONF_DIR">[`NIX_CONF_DIR`](#env-NIX_CONF_DIR)</span>

  Overrides the location of the system Nix configuration directory
  (default `sysconfdir/nix`, i.e. `/etc/nix` on most systems).

- <span id="env-NIX_CONFIG">[`NIX_CONFIG`](#env-NIX_CONFIG)</span>

  Applies settings from Nix configuration from the environment.
  The content is treated as if it was read from a Nix configuration file.
  Settings are separated by the newline character.

- <span id="env-NIX_USER_CONF_FILES">[`NIX_USER_CONF_FILES`](#env-NIX_USER_CONF_FILES)</span>

  Overrides the location of the Nix user configuration files to load from.

  The default are the locations according to the [XDG Base Directory Specification].
  See the [XDG Base Directories](#xdg-base-directories) sub-section for details.

  The variable is treated as a list separated by the `:` token.

- <span id="env-TMPDIR">[`TMPDIR`](#env-TMPDIR)</span>

  Use the specified directory to store temporary files. In particular,
  this includes temporary build directories; these can take up
  substantial amounts of disk space. The default is `/tmp`.

- <span id="env-NIX_REMOTE">[`NIX_REMOTE`](#env-NIX_REMOTE)</span>

  This variable should be set to `daemon` if you want to use the Nix
  daemon to execute Nix operations. This is necessary in [multi-user
  Nix installations](@docroot@/installation/multi-user.md). If the Nix
  daemon's Unix socket is at some non-standard path, this variable
  should be set to `unix://path/to/socket`. Otherwise, it should be
  left unset.

- <span id="env-NIX_SHOW_STATS">[`NIX_SHOW_STATS`](#env-NIX_SHOW_STATS)</span>

  If set to `1`, Nix will print some evaluation statistics, such as
  the number of values allocated.

- <span id="env-NIX_COUNT_CALLS">[`NIX_COUNT_CALLS`](#env-NIX_COUNT_CALLS)</span>

  If set to `1`, Nix will print how often functions were called during
  Nix expression evaluation. This is useful for profiling your Nix
  expressions.

- <span id="env-GC_INITIAL_HEAP_SIZE">[`GC_INITIAL_HEAP_SIZE`](#env-GC_INITIAL_HEAP_SIZE)</span>

  If Nix has been configured to use the Boehm garbage collector, this
  variable sets the initial size of the heap in bytes. It defaults to
  384 MiB. Setting it to a low value reduces memory consumption, but
  will increase runtime due to the overhead of garbage collection.

## XDG Base Directories

Nix follows the [XDG Base Directory Specification].

For backwards compatibility, Nix commands will follow the standard only when [`use-xdg-base-directories`] is enabled.
[New Nix commands](@docroot@/command-ref/new-cli/nix.md) (experimental) conform to the standard by default.

The following environment variables are used to determine locations of various state and configuration files:

- [`XDG_CONFIG_HOME`]{#env-XDG_CONFIG_HOME} (default `~/.config`)
- [`XDG_STATE_HOME`]{#env-XDG_STATE_HOME} (default `~/.local/state`)
- [`XDG_CACHE_HOME`]{#env-XDG_CACHE_HOME} (default `~/.cache`)

[XDG Base Directory Specification]: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
[`use-xdg-base-directories`]: @docroot@/command-ref/conf-file.md#conf-use-xdg-base-directories

In addition, setting the following environment variables overrides the XDG base directories:

- [`NIX_CONFIG_HOME`]{#env-NIX_CONFIG_HOME} (default `$XDG_CONFIG_HOME/nix`)
- [`NIX_STATE_HOME`]{#env-NIX_STATE_HOME} (default `$XDG_STATE_HOME/nix`)
- [`NIX_CACHE_HOME`]{#env-NIX_CACHE_HOME} (default `$XDG_CACHE_HOME/nix`)

When [`use-xdg-base-directories`] is enabled, the configuration directory is:

1. `$NIX_CONFIG_HOME`, if it is defined
2. Otherwise, `$XDG_CONFIG_HOME/nix`, if `XDG_CONFIG_HOME` is defined
3. Otherwise, `~/.config/nix`.

Likewise for the state and cache directories.
