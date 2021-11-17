# Copying Closures via SSH

The command `nix-copy-closure` copies a Nix store path along with all
its dependencies to or from another machine via the SSH protocol. It
doesn’t copy store paths that are already present on the target machine.
For example, the following command copies Firefox with all its
dependencies:

    $ nix-copy-closure --to alice@itchy.example.org $(type -p firefox)

See the [manpage for `nix-copy-closure`](../command-ref/nix-copy-closure.md) for details.

With `nix-store
--export` and `nix-store --import` you can write the closure of a store
path (that is, the path and all its dependencies) to a file, and then
unpack that file into another Nix store. For example,

    $ nix-store --export $(nix-store -qR $(type -p firefox)) > firefox.closure

writes the closure of Firefox to a file. You can then copy this file to
another machine and install the closure:

    $ nix-store --import < firefox.closure

Any store paths in the closure that are already present in the target
store are ignored. It is also possible to pipe the export into another
command, e.g. to copy and install a closure directly to/on another
machine:

    $ nix-store --export $(nix-store -qR $(type -p firefox)) | bzip2 | \
        ssh alice@itchy.example.org "bunzip2 | nix-store --import"

However, `nix-copy-closure` is generally more efficient because it only
copies paths that are not already present in the target Nix store.
