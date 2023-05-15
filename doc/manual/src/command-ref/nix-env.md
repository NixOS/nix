# Name

`nix-env` - manipulate or query Nix user environments

# Synopsis

`nix-env` *operation* [*options*] [*arguments…*]
  [`--option` *name* *value*]
  [`--arg` *name* *value*]
  [`--argstr` *name* *value*]
  [{`--file` | `-f`} *path*]
  [{`--profile` | `-p`} *path*]
  [`--system-filter` *system*]
  [`--dry-run`]

# Description

The command `nix-env` is used to manipulate Nix user environments. User
environments are sets of software packages available to a user at some
point in time. In other words, they are a synthesised view of the
programs available in the Nix store. There may be many user
environments: different users can have different environments, and
individual users can switch between different environments.

`nix-env` takes exactly one *operation* flag which indicates the
subcommand to be performed. The following operations are available:

- [`--install`](./nix-env/install.md)
- [`--upgrade`](./nix-env/upgrade.md)
- [`--uninstall`](./nix-env/uninstall.md)
- [`--set`](./nix-env/set.md)
- [`--set-flag`](./nix-env/set-flag.md)
- [`--query`](./nix-env/query.md)
- [`--switch-profile`](./nix-env/switch-profile.md)
- [`--list-generations`](./nix-env/list-generations.md)
- [`--delete-generations`](./nix-env/delete-generations.md)
- [`--switch-generation`](./nix-env/switch-generation.md)
- [`--rollback`](./nix-env/rollback.md)

These pages can be viewed offline:

- `man nix-env-<operation>`.

  Example: `man nix-env-install`

- `nix-env --help --<operation>`

  Example: `nix-env --help --install`

# Selectors

Several commands, such as `nix-env -q` and `nix-env -i`, take a list of
arguments that specify the packages on which to operate. These are
extended regular expressions that must match the entire name of the
package. (For details on regular expressions, see **regex**(7).) The match is
case-sensitive. The regular expression can optionally be followed by a
dash and a version number; if omitted, any version of the package will
match. Here are some examples:

  - `firefox`\
    Matches the package name `firefox` and any version.

  - `firefox-32.0`\
    Matches the package name `firefox` and version `32.0`.

  - `gtk\\+`\
    Matches the package name `gtk+`. The `+` character must be escaped
    using a backslash to prevent it from being interpreted as a
    quantifier, and the backslash must be escaped in turn with another
    backslash to ensure that the shell passes it on.

  - `.\*`\
    Matches any package name. This is the default for most commands.

  - `'.*zip.*'`\
    Matches any package name containing the string `zip`. Note the dots:
    `'*zip*'` does not work, because in a regular expression, the
    character `*` is interpreted as a quantifier.

  - `'.*(firefox|chromium).*'`\
    Matches any package name containing the strings `firefox` or
    `chromium`.

# Files

{{#include ./files/default-nix-expression.md}}

{{#include ./files/profiles.md}}
