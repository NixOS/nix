# Name

`nix-store` - manipulate or query the Nix store

# Synopsis

`nix-store` *operation* [*options…*] [*arguments…*]
  [`--option` *name* *value*]
  [`--add-root` *path*]

# Description

The command `nix-store` performs primitive operations on the Nix store.
You generally do not need to run this command manually.

`nix-store` takes exactly one *operation* flag which indicates the subcommand to be performed. The following operations are available:

- [`--realise`](./nix-store/realise.md)
- [`--serve`](./nix-store/serve.md)
- [`--gc`](./nix-store/gc.md)
- [`--delete`](./nix-store/delete.md)
- [`--query`](./nix-store/query.md)
- [`--add`](./nix-store/add.md)
- [`--add-fixed`](./nix-store/add-fixed.md)
- [`--verify`](./nix-store/verify.md)
- [`--verify-path`](./nix-store/verify-path.md)
- [`--repair-path`](./nix-store/repair-path.md)
- [`--dump`](./nix-store/dump.md)
- [`--restore`](./nix-store/restore.md)
- [`--export`](./nix-store/export.md)
- [`--import`](./nix-store/import.md)
- [`--optimise`](./nix-store/optimise.md)
- [`--read-log`](./nix-store/read-log.md)
- [`--dump-db`](./nix-store/dump-db.md)
- [`--load-db`](./nix-store/load-db.md)
- [`--print-env`](./nix-store/print-env.md)
- [`--generate-binary-cache-key`](./nix-store/generate-binary-cache-key.md)

These pages can be viewed offline:

- `man nix-store-<operation>`.

  Example: `man nix-store-realise`

- `nix-store --help --<operation>`

  Example: `nix-store --help --realise`
