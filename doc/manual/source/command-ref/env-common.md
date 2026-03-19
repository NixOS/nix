# Common Environment Variables

Most Nix commands interpret the following environment variables.

## Configuration environment variables

The following environment variables affect how Nix loads its configuration.
See the [configuration file](@docroot@/command-ref/conf-file.md#configuration-file) documentation for full details.

- <span id="env-NIX_CONF_DIR">[`NIX_CONF_DIR`](#env-NIX_CONF_DIR)</span>

  Overrides the system configuration directory.

- <span id="env-NIX_USER_CONF_FILES">[`NIX_USER_CONF_FILES`](#env-NIX_USER_CONF_FILES)</span>

  Overrides the user configuration file locations.

- <span id="env-NIX_CONFIG">[`NIX_CONFIG`](#env-NIX_CONFIG)</span>

  Provides configuration settings inline.

## Store setting environment variables

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

  Overrides the location of the Nix store.
  On Unix, the default is `/nix/store`, set at compile time by the `libstore:store-dir` build option.
  On Windows, there is no compile-time setting; the default is `%PROGRAMDATA%\nix\store`, determined at runtime using the [`%PROGRAMDATA%` Known Folder](#known-folders).

  See the [Store Types] chapter; each store has a `store` setting linking back to this as its default.

- <span id="env-NIX_LOG_DIR">[`NIX_LOG_DIR`](#env-NIX_LOG_DIR)</span>

  Overrides the location of the Nix log directory.
  On Unix, the default is `/nix/var/log/nix`, set at compile time by the `log-dir` build option.
  On Windows, there is no compile-time setting; the default is `%PROGRAMDATA%\nix\log`, determined at runtime using the [`%PROGRAMDATA%` Known Folder](#known-folders).

  The [Local Store], [Local Daemon Store], and [Experimental SSH Store with filesystem mounted] have per-store settings that override this.

- <span id="env-NIX_STATE_DIR">[`NIX_STATE_DIR`](#env-NIX_STATE_DIR)</span>

  Overrides the location of the Nix state directory.
  On Unix, the default is `${localstatedir}/nix`, where `localstatedir` is a compile-time build option defaulting to `/nix/var`.
  On Windows, there is no compile-time setting; the default is `%PROGRAMDATA%\nix\state`, determined at runtime using the [`%PROGRAMDATA%` Known Folder](#known-folders).

  The [Local Store], [Local Daemon Store], and [Experimental SSH Store with filesystem mounted] have per-store settings that override this.

- <span id="env-NIX_DAEMON_SOCKET_PATH">[`NIX_DAEMON_SOCKET_PATH`](#env-NIX_DAEMON_SOCKET_PATH)</span>

  Overrides the path to the Unix domain socket used to communicate with the Nix daemon.
  Defaults to `daemon-socket/socket` within the state directory (see [`NIX_STATE_DIR`](#env-NIX_STATE_DIR)).

  See the [Local Daemon Store] documentation for details on how the socket path is resolved.

- <span id="env-TMPDIR">[`TMPDIR`](#env-TMPDIR)</span>

  Use the specified directory to store temporary files. In particular,
  this includes temporary build directories; these can take up
  substantial amounts of disk space. The default is `/tmp`.

- <span id="env-NIX_REMOTE">[`NIX_REMOTE`](#env-NIX_REMOTE)</span>

  Overrides the [`store`](@docroot@/command-ref/conf-file.md#conf-store) setting.

[Store Types]: @docroot@/store/types/index.md
[store URL format]: @docroot@/store/types/index.md#store-url-format
[Local Store]: @docroot@/store/types/local-store.md
[Local Daemon Store]: @docroot@/store/types/local-daemon-store.md
[Experimental SSH Store with filesystem mounted]: @docroot@/store/types/experimental-ssh-store-with-filesystem-mounted.md

## Nix language evaluator settings

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

- <span id="env-NIX_PATH">[`NIX_PATH`](#env-NIX_PATH)</span>

  A colon-separated list of search path entries used to resolve [lookup paths](@docroot@/language/constructs/lookup-path.md).

  This environment variable overrides the value of the [`nix-path` configuration setting](@docroot@/command-ref/conf-file.md#conf-nix-path).

  It can be extended using the [`-I` option](@docroot@/command-ref/opt-common.md#opt-I).

  > **Example**
  >
  > ```bash
  > $ export NIX_PATH=/home/eelco/Dev:nixos-config=/etc/nixos
  > ```

  If `NIX_PATH` is set to an empty string, resolving search paths will always fail.

  > **Example**
  >
  > ```bash
  > $ NIX_PATH= nix-instantiate --eval '<nixpkgs>'
  > error: file 'nixpkgs' was not found in the Nix search path (add it using $NIX_PATH or -I)
  > ```

## User Directories

Nix supports per-user **configuration**, **state**, and **cache** directories.
The following environment variables override the locations for these directories:

- [user configuration directory]{#user-conf-dir}: [`NIX_CONFIG_HOME`]{#env-NIX_CONFIG_HOME}
- user state directory: [`NIX_STATE_HOME`]{#env-NIX_STATE_HOME}
- user cache directory: [`NIX_CACHE_HOME`]{#env-NIX_CACHE_HOME}

When these are not set, the defaults depend on the platform:

- On Unix, the [XDG base directories](#xdg-base-directories): `$XDG_CONFIG_HOME/nix`, `$XDG_STATE_HOME/nix`, `$XDG_CACHE_HOME/nix`
- On Windows, the [Windows Known Folders](#known-folders): `%APPDATA%\nix\config`, `%LOCALAPPDATA%\nix\state`, `%LOCALAPPDATA%\nix\cache`

[`use-xdg-base-directories`]: @docroot@/command-ref/conf-file.md#conf-use-xdg-base-directories

For backwards compatibility, legacy Nix commands (e.g. `nix-env`, `nix-channel`) use dotfiles in `$HOME` instead of these directories unless [`use-xdg-base-directories`] is enabled.
[New Nix commands](@docroot@/command-ref/new-cli/nix.md) (experimental) use the proper directories by default.

When [`use-xdg-base-directories`] is enabled, the configuration directory is resolved as:

1. `$NIX_CONFIG_HOME`, if it is defined
2. Otherwise, the platform default (e.g. `$XDG_CONFIG_HOME/nix` on Unix)

Likewise for the state and cache directories.

## Miscellanous environment variables

- <span id="env-IN_NIX_SHELL">[`IN_NIX_SHELL`](#env-IN_NIX_SHELL)</span>

  Indicator that tells if the current environment was set up by
  `nix-shell`. It can have the values `pure` or `impure`.

## Appendix: OS-specific conventions

This information is not Nix-specific, but is referenced above.

### Unix: XDG Base Directories {#xdg-base-directories}

The [XDG Base Directory Specification] defines standard locations for user-specific configuration, state, and cache files on Unix systems.

[XDG Base Directory Specification]: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

The following environment variables are used:

- [`XDG_CONFIG_HOME`]{#env-XDG_CONFIG_HOME} (default `~/.config`)
- [`XDG_STATE_HOME`]{#env-XDG_STATE_HOME} (default `~/.local/state`)
- [`XDG_CACHE_HOME`]{#env-XDG_CACHE_HOME} (default `~/.cache`)
- [`XDG_CONFIG_DIRS`]{#env-XDG_CONFIG_DIRS} (default `/etc/xdg`) — colon-separated list of additional configuration base directories, searched after `XDG_CONFIG_HOME`

### Windows: Known Folders {#known-folders}

On Windows, [Known Folders][windows-known-folders] provide standard locations for application data on Windows.

[windows-known-folders]: https://learn.microsoft.com/en-us/windows/win32/shell/known-folders

The relevant folders are:

- [`%APPDATA%`]{#env-APPDATA} — per-user roaming application data
- [`%LOCALAPPDATA%`]{#env-LOCALAPPDATA} — per-user local application data
- [`%PROGRAMDATA%`]{#env-PROGRAMDATA} — system-wide application data
