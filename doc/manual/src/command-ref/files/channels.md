## Channels

A directory containing symlinks to Nix channels, managed by [`nix-channel`].

The channels directory is a [profile](@docroot@/command-ref/files/profiles.md), so as to allow easy management of multiple versions and switching between them.
This profile contains symlinks to the contents of those channels.

### User-specific and global channels

Channels are managed either for a specific user, or for all users globally on the system to share.
This matches the [user-specific vs global conventions](@docroot@/command-ref/files/profiles.md#user-specific-and-global-profiles) of profiles themselves.

- [User-specific channels]{#user-channels} are stored in:
  ```
  $XDG_STATE_HOME/nix/profiles/channels
  ```

- [Global channels]{#global-channels} are stored in
  ```
  $NIX_STATE_DIR/profiles/per-user/root/channels
  ```

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
