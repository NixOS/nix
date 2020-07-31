# Quick Start

This chapter is for impatient people who don't like reading
documentation.  For more in-depth information you are kindly referred
to subsequent chapters.

1. Install single-user Nix by running the following:

   ```console
   $ bash <(curl -L https://nixos.org/nix/install)
   ```

   This will install Nix in `/nix`. The install script will create
   `/nix` using `sudo`, so make sure you have sufficient rights.  (For
   other installation methods, see
   [here](installation/installation.md).)

1. See what installable packages are currently available in the
   channel:

   ```console
   $ nix-env -qa
   docbook-xml-4.3
   docbook-xml-4.5
   firefox-33.0.2
   hello-2.9
   libxslt-1.1.28
   …
   ```

1. Install some packages from the channel:

   ```console
   $ nix-env -i hello
   ```

   This should download pre-built packages; it should not build them
   locally (if it does, something went wrong).

1. Test that they work:

   ```console
   $ which hello
   /home/eelco/.nix-profile/bin/hello
   $ hello
   Hello, world!
   ```

1. Uninstall a package:

   ```console
   $ nix-env -e hello
   ```

1. You can also test a package without installing it:

   ```console
   $ nix-shell -p hello
   ```

   This builds or downloads GNU Hello and its dependencies, then drops
   you into a Bash shell where the `hello` command is present, all
   without affecting your normal environment:

   ```console
   [nix-shell:~]$ hello
   Hello, world!

   [nix-shell:~]$ exit

   $ hello
   hello: command not found
   ```

1. To keep up-to-date with the channel, do:

   ```console
   $ nix-channel --update nixpkgs
   $ nix-env -u '*'
   ```

   The latter command will upgrade each installed package for which
   there is a “newer” version (as determined by comparing the version
   numbers).

1. If you're unhappy with the result of a `nix-env` action (e.g., an
   upgraded package turned out not to work properly), you can go back:

   ```console
   $ nix-env --rollback
   ```

1. You should periodically run the Nix garbage collector to get rid of
   unused packages, since uninstalls or upgrades don't actually delete
   them:

   ```console
   $ nix-collect-garbage -d
   ```
