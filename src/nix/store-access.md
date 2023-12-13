R"(
# Description

`nix store access` provides subcommands that query and manipulate access control lists (ACLs) of store paths.
ACLs allow for granular access to the nix store: paths can be protected from all users (`nix store access protect`), and then necessary users can be granted permission to those paths (`nix store access grant`).

Under the hood, `nix store access` uses POSIX ACLs.

<!-- FIXME moar docs -->

)"
