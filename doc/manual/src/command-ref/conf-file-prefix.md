# Name

`nix.conf` - Nix configuration file

# Description

By default Nix reads settings from the following places:

  - The system-wide configuration file `sysconfdir/nix/nix.conf` (i.e.
    `/etc/nix/nix.conf` on most systems), or `$NIX_CONF_DIR/nix.conf` if
    `NIX_CONF_DIR` is set. Values loaded in this file are not forwarded
    to the Nix daemon. The client assumes that the daemon has already
    loaded them.

  - If `NIX_USER_CONF_FILES` is set, then each path separated by `:`
    will be loaded in reverse order.

    Otherwise it will look for `nix/nix.conf` files in `XDG_CONFIG_DIRS`
    and `XDG_CONFIG_HOME`. If these are unset, it will look in
    `$HOME/.config/nix/nix.conf`.

  - If `NIX_CONFIG` is set, its contents is treated as the contents of
    a configuration file.

The configuration files consist of `name = value` pairs, one per
line. Other files can be included with a line like `include path`,
where *path* is interpreted relative to the current conf file and a
missing file is an error unless `!include` is used instead. Comments
start with a `#` character. Here is an example configuration file:

    keep-outputs = true       # Nice for developers
    keep-derivations = true   # Idem

You can override settings on the command line using the `--option`
flag, e.g. `--option keep-outputs false`. Every configuration setting
also has a corresponding command line flag, e.g. `--max-jobs 16`; for
Boolean settings, there are two flags to enable or disable the setting
(e.g. `--keep-failed` and `--no-keep-failed`).

A configuration setting usually overrides any previous value. However,
you can prefix the name of the setting by `extra-` to *append* to the
previous value. For instance,

    substituters = a b
    extra-substituters = c d

defines the `substituters` setting to be `a b c d`. This is also
available as a command line flag (e.g. `--extra-substituters`).

The following settings are currently available:

