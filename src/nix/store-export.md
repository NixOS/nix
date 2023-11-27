R""(

# Examples

* Export the closure of a given installable and re-import it in another machine

  ```console
  $ nix store export --recursive --format binary nixpkgs#hello > hello-closure.tar
  $ ssh user@otherHost nix store import < hello-closure.tar
  ```

# Description

This command generates an archive containing the serialisation of *installable*, as well as all the metadata required so that it can be imported with [`nix store import`](@docroot@/command-ref/new-cli/nix3-store-import.md).
)""
