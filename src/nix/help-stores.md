R"(

Nix supports different types of stores. These are described below.

## Store URL format

Stores are specified using a URL-like syntax. For example, the command

```console
# nix path-info --store https://cache.nixos.org/ --json \
  /nix/store/a7gvj343m05j2s32xcnwr35v31ynlypr-coreutils-9.1
```

fetches information about a store path in the HTTP binary cache
located at https://cache.nixos.org/, which is a type of store.

Store URLs can specify **store settings** using URL query strings,
i.e. by appending `?name1=value1&name2=value2&...` to the URL. For
instance,

```
--store ssh://machine.example.org?ssh-key=/path/to/my/key
```

tells Nix to access the store on a remote machine via the SSH
protocol, using `/path/to/my/key` as the SSH private key. The
supported settings for each store type are documented below.

The special store URL `auto` causes Nix to automatically select a
store as follows:

* Use the [local store](#local-store) `/nix/store` if `/nix/var/nix`
  is writable by the current user.

* Otherwise, if `/nix/var/nix/daemon-socket/socket` exists, [connect
  to the Nix daemon listening on that socket](#local-daemon-store).

* Otherwise, on Linux only, use the [local chroot store](#local-store)
  `~/.local/share/nix/root`, which will be created automatically if it
  does not exist.

* Otherwise, use the [local store](#local-store) `/nix/store`.

@stores@

)"
