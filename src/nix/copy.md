R""(

# Examples

* Copy Firefox from the local store to a binary cache in `/tmp/cache`:

  ```console
  # nix copy --to file:///tmp/cache $(type -p firefox)
  ```

  Note the `file://` - without this, the destination is a chroot
  store, not a binary cache.

* Copy the entire current NixOS system closure to another machine via
  SSH:

  ```console
  # nix copy -s --to ssh://server /run/current-system
  ```

  The `-s` flag causes the remote machine to try to substitute missing
  store paths, which may be faster if the link between the local and
  remote machines is slower than the link between the remote machine
  and its substituters (e.g. `https://cache.nixos.org`).

* Copy a closure from another machine via SSH:

  ```console
  # nix copy --from ssh://server /nix/store/a6cnl93nk1wxnq84brbbwr6hxw9gp2w9-blender-2.79-rc2
  ```

* Copy Hello to a binary cache in an Amazon S3 bucket:

  ```console
  # nix copy --to s3://my-bucket?region=eu-west-1 nixpkgs#hello
  ```

  or to an S3-compatible storage system:

  ```console
  # nix copy --to s3://my-bucket?region=eu-west-1&endpoint=example.com nixpkgs#hello
  ```

  Note that this only works if Nix is built with AWS support.

* Copy a closure from `/nix/store` to the chroot store `/tmp/nix/nix/store`:

  ```console
  # nix copy --to /tmp/nix nixpkgs#hello --no-check-sigs
  ```

# Description

`nix copy` copies store path closures between two Nix stores. The
source store is specified using `--from` and the destination using
`--to`. If one of these is omitted, it defaults to the local store.

)""
