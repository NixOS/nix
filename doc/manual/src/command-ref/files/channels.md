## Channels

Channels are a mechanism for obtaining Nix expressions over the web.

The moving parts of channels are:
- The official channels listed at <https://nixos.org/channels>
- The user-specific list of [subscribed channels](#subscribed-channels)
- The [downloaded channel contents](#channel-contents)
- The [Nix expression search path](@docroot@/command-ref/conf-file.md#conf-nix-path)

> **Note**
>
> The state of a subscribed channel is external to the Nix expressions relying on it.
> This may limit reproducibility.
>
> Dependencies on other Nix expressions can be declared explicitly with:
> - [`fetchurl`](@docroot@/language/builtins.md#builtins-fetchurl), [`fetchTarball`](@docroot@/language/builtins.md#builtins-fetchTarball), or [`fetchGit`](@docroot@/language/builtins.md#builtins-fetchGit) in Nix expressions
> - the [`-I` option](@docroot@/command-ref/opt-common.md#opt-I) in command line invocations for explicitly determining the value of [lookup paths](@docroot@/language/constructs/lookup-path.md)

## Subscribed channels

The list of subscribed channels is stored in a file:

- `~/.nix-channels`
- `$XDG_STATE_HOME/nix/channels` if [`use-xdg-base-directories`](@docroot@/command-ref/conf-file.md#conf-use-xdg-base-directories) is set to `true`

Each line maps a channel name to a URL in the following format:

```
<url> <name>
```

## Channels contents

The [`nix-channel`](@docroot@/command-ref/nix-channel.md) command uses a [profile](@docroot@/command-ref/files/profiles.md) to keep track of channels:

- `$XDG_STATE_HOME/nix/profiles/channels` for regular users
- `$NIX_STATE_DIR/profiles/per-user/root/channels` for `root`

Each generation of that profile is a directory with symlinks to channel contents.
Each entry in this directory corresponds to the name of a [subscribed channel](#subscribed-channels) at that time.
