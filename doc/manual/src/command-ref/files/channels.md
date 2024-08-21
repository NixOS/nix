## Channels

A directory containing symlinks to Nix channels, managed by [`nix-channel`]:

- `$XDG_STATE_HOME/nix/profiles/channels` for regular users
- `$NIX_STATE_DIR/profiles/per-user/root/channels` for `root`

[`nix-channel`] uses a [profile](@docroot@/command-ref/files/profiles.md) to store channels.
This profile contains symlinks to the contents of those channels.

## Subscribed channels

The list of subscribed channels is stored in

- `~/.nix-channels`
- `$XDG_STATE_HOME/nix/channels` if [`use-xdg-base-directories`] is set to `true`

in the following format:

```
<url> <name>
...
```

[`nix-channel`]: @docroot@/command-ref/nix-channel.md
[`use-xdg-base-directories`]: @docroot@/command-ref/conf-file.md#conf-use-xdg-base-directories
