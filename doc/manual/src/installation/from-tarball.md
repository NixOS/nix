# Installing from a binary tarball

> PR_COMMENT TODO Add reference to the documentation of the install script.

You can also download a binary tarball that contains Nix and all its
dependencies. (This is what the install script at
<https://nixos.org/nix/install> does automatically.) You should unpack
it somewhere (e.g. in `/tmp`), and then run the script named `install`
inside the binary tarball:

```console
$ cd /tmp
$ tar xfj nix-1.8-x86_64-darwin.tar.bz2
$ cd nix-1.8-x86_64-darwin
$ ./install
```

If you need to edit the multi-user installation script to use different
group ID or a different user ID range, modify the variables set in the
file named `install-multi-user`.
