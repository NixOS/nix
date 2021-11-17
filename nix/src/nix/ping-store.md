R""(

# Examples

* Test whether connecting to a remote Nix store via SSH works:

  ```console
  # nix store ping --store ssh://mac1
  ```

* Test whether a URL is a valid binary cache:

  ```console
  # nix store ping --store https://cache.nixos.org
  ```

* Test whether the Nix daemon is up and running:

  ```console
  # nix store ping --store daemon
  ```

# Description

This command tests whether a particular Nix store (specified by the
argument `--store` *url*) can be accessed. What this means is
dependent on the type of the store. For instance, for an SSH store it
means that Nix can connect to the specified machine.

If the command succeeds, Nix returns a exit code of 0 and does not
print any output.

)""
