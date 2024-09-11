# Name

`nix.conf` - Nix configuration file

# Description

Nix supports a variety of configuration settings, which are read from configuration files or taken as command line flags.

## Configuration file

By default Nix reads settings from the following places, in that order:

1. The system-wide configuration file `sysconfdir/nix/nix.conf` (i.e. `/etc/nix/nix.conf` on most systems), or `$NIX_CONF_DIR/nix.conf` if [`NIX_CONF_DIR`](./env-common.md#env-NIX_CONF_DIR) is set.

   Values loaded in this file are not forwarded to the Nix daemon.
   The client assumes that the daemon has already loaded them.

1. If [`NIX_USER_CONF_FILES`](./env-common.md#env-NIX_USER_CONF_FILES) is set, then each path separated by `:` will be loaded in reverse order.

   Otherwise it will look for `nix/nix.conf` files in `XDG_CONFIG_DIRS` and [`XDG_CONFIG_HOME`](./env-common.md#env-XDG_CONFIG_HOME).
   If unset, `XDG_CONFIG_DIRS` defaults to `/etc/xdg`, and `XDG_CONFIG_HOME` defaults to `$HOME/.config` as per [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html).

1. If [`NIX_CONFIG`](./env-common.md#env-NIX_CONFIG) is set, its contents are treated as the contents of a configuration file.

### File format

Configuration files consist of `name = value` pairs, one per line.
Comments start with a `#` character.

Example:

```
keep-outputs = true       # Nice for developers
keep-derivations = true   # Idem
```

Other files can be included with a line like `include <path>`, where `<path>` is interpreted relative to the current configuration file.
A missing file is an error unless `!include` is used instead.

A configuration setting usually overrides any previous value.
However, for settings that take a list of items, you can prefix the name of the setting by `extra-` to *append* to the previous value.

For instance,

```
substituters = a b
extra-substituters = c d
```

defines the `substituters` setting to be `a b c d`.

Unknown option names are not an error, and are simply ignored with a warning.

## Command line flags

Configuration options can be set on the command line, overriding the values set in the [configuration file](#configuration-file):

- Every configuration setting has corresponding command line flag (e.g. `--max-jobs 16`).
  Boolean settings do not need an argument, and can be explicitly disabled with the `no-` prefix (e.g. `--keep-failed` and `--no-keep-failed`).

  Unknown option names are invalid flags (unless there is already a flag with that name), and are rejected with an error.

- The flag `--option <name> <value>` is interpreted exactly like a `<name> = <value>` in a setting file.

  Unknown option names are ignored with a warning.

The `extra-` prefix is supported for settings that take a list of items (e.g. `--extra-trusted users alice` or `--option extra-trusted-users alice`).

## Integer settings

Settings that have an integer type support the suffixes `K`, `M`, `G`
and `T`. These cause the specified value to be multiplied by 2^10,
2^20, 2^30 and 2^40, respectively. For instance, `--min-free 1M` is
equivalent to `--min-free 1048576`.

# Available settings

