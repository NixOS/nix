R""(

# Examples

* Import a closure that has been exported from another machine

  ```console
  $ ssh user@otherHost nix store export --recursive --format binary nixpkgs#hello > hello-closure.tar
  $ nix store import < hello-closure.tar
  ```

# Description

This command reads an archive of store paths, as produced by [`nix store export`](@docroot@/command-ref/new-cli/nix3-store-export.md), and adds it to the store.

)""
