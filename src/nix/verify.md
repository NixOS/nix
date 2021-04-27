R""(

# Examples

* Verify the entire Nix store:

  ```console
  # nix store verify --all
  ```

* Check whether each path in the closure of Firefox has at least 2
  signatures:

  ```console
  # nix store verify -r -n2 --no-contents $(type -p firefox)
  ```

* Verify a store path in the binary cache `https://cache.nixos.org/`:

  ```console
  # nix store verify --store https://cache.nixos.org/ \
      /nix/store/v5sv61sszx301i0x6xysaqzla09nksnd-hello-2.10
  ```

# Description

This command verifies the integrity of the store paths *installables*,
or, if `--all` is given, the entire Nix store. For each path, it
checks that

* its contents match the NAR hash recorded in the Nix database; and

* it is *trusted*, that is, it is signed by at least one trusted
  signing key, is content-addressed, or is built locally ("ultimately
  trusted").

# Exit status

The exit status of this command is the sum of the following values:

* **1** if any path is corrupted (i.e. its contents don't match the
  recorded NAR hash).

* **2** if any path is untrusted.

* **4** if any path couldn't be verified for any other reason (such as
  an I/O error).

)""
