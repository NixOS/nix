# Release 1.7 (2014-04-11)

In addition to the usual bug fixes, this release has the following new
features:

  - Antiquotation is now allowed inside of quoted attribute names (e.g.
    `set."${foo}"`). In the case where the attribute name is just a
    single antiquotation, the quotes can be dropped (e.g. the above
    example can be written `set.${foo}`). If an attribute name inside of
    a set declaration evaluates to `null` (e.g. `{ ${null} = false; }`),
    then that attribute is not added to the set.

  - Experimental support for cryptographically signed binary caches. See
    [the commit for
    details](https://github.com/NixOS/nix/commit/0fdf4da0e979f992db75cc17376e455ddc5a96d8).

  - An experimental new substituter, `download-via-ssh`, that fetches
    binaries from remote machines via SSH. Specifying the flags
    `--option
                    use-ssh-substituter true --option ssh-substituter-hosts
                    user@hostname` will cause Nix to download binaries from the
    specified machine, if it has them.

  - `nix-store -r` and `nix-build` have a new flag, `--check`, that
    builds a previously built derivation again, and prints an error
    message if the output is not exactly the same. This helps to verify
    whether a derivation is truly deterministic. For example:
    
        $ nix-build '<nixpkgs>' -A patchelf
        …
        $ nix-build '<nixpkgs>' -A patchelf --check
        …
        error: derivation `/nix/store/1ipvxs…-patchelf-0.6' may not be deterministic:
          hash mismatch in output `/nix/store/4pc1dm…-patchelf-0.6.drv'

  - The `nix-instantiate` flags `--eval-only` and `--parse-only` have
    been renamed to `--eval` and `--parse`, respectively.

  - `nix-instantiate`, `nix-build` and `nix-shell` now have a flag
    `--expr` (or `-E`) that allows you to specify the expression to be
    evaluated as a command line argument. For instance, `nix-instantiate
    --eval -E
                    '1 + 2'` will print `3`.

  - `nix-shell` improvements:
    
      - It has a new flag, `--packages` (or `-p`), that sets up a build
        environment containing the specified packages from Nixpkgs. For
        example, the command
        
            $ nix-shell -p sqlite xorg.libX11 hello
        
        will start a shell in which the given packages are present.
    
      - It now uses `shell.nix` as the default expression, falling back
        to `default.nix` if the former doesn’t exist. This makes it
        convenient to have a `shell.nix` in your project to set up a
        nice development environment.
    
      - It evaluates the derivation attribute `shellHook`, if set. Since
        `stdenv` does not normally execute this hook, it allows you to
        do `nix-shell`-specific setup.
    
      - It preserves the user’s timezone setting.

  - In chroots, Nix now sets up a `/dev` containing only a minimal set
    of devices (such as `/dev/null`). Note that it only does this if you
    *don’t* have `/dev` listed in your `build-chroot-dirs` setting;
    otherwise, it will bind-mount the `/dev` from outside the chroot.
    
    Similarly, if you don’t have `/dev/pts` listed in
    `build-chroot-dirs`, Nix will mount a private `devpts` filesystem on
    the chroot’s `/dev/pts`.

  - New built-in function: `builtins.toJSON`, which returns a JSON
    representation of a value.

  - `nix-env -q` has a new flag `--json` to print a JSON representation
    of the installed or available packages.

  - `nix-env` now supports meta attributes with more complex values,
    such as attribute sets.

  - The `-A` flag now allows attribute names with dots in them, e.g.
    
        $ nix-instantiate --eval '<nixos>' -A 'config.systemd.units."nscd.service".text'

  - The `--max-freed` option to `nix-store --gc` now accepts a unit
    specifier. For example, `nix-store --gc --max-freed
                    1G` will free up to 1 gigabyte of disk space.

  - `nix-collect-garbage` has a new flag `--delete-older-than` *N*`d`,
    which deletes all user environment generations older than *N* days.
    Likewise, `nix-env
                    --delete-generations` accepts a *N*`d` age limit.

  - Nix now heuristically detects whether a build failure was due to a
    disk-full condition. In that case, the build is not flagged as
    “permanently failed”. This is mostly useful for Hydra, which needs
    to distinguish between permanent and transient build failures.

  - There is a new symbol `__curPos` that expands to an attribute set
    containing its file name and line and column numbers, e.g. `{ file =
    "foo.nix"; line = 10;
                    column = 5; }`. There also is a new builtin function,
    `unsafeGetAttrPos`, that returns the position of an attribute. This
    is used by Nixpkgs to provide location information in error
    messages, e.g.
    
        $ nix-build '<nixpkgs>' -A libreoffice --argstr system x86_64-darwin
        error: the package ‘libreoffice-4.0.5.2’ in ‘.../applications/office/libreoffice/default.nix:263’
          is not supported on ‘x86_64-darwin’

  - The garbage collector is now more concurrent with other Nix
    processes because it releases certain locks earlier.

  - The binary tarball installer has been improved. You can now install
    Nix by running:
    
        $ bash <(curl https://nixos.org/nix/install)

  - More evaluation errors include position information. For instance,
    selecting a missing attribute will print something like
    
        error: attribute `nixUnstabl' missing, at /etc/nixos/configurations/misc/eelco/mandark.nix:216:15

  - The command `nix-setuid-helper` is gone.

  - Nix no longer uses Automake, but instead has a non-recursive, GNU
    Make-based build system.

  - All installed libraries now have the prefix `libnix`. In particular,
    this gets rid of `libutil`, which could clash with libraries with
    the same name from other packages.

  - Nix now requires a compiler that supports C++11.

This release has contributions from Danny Wilson, Domen Kožar, Eelco
Dolstra, Ian-Woo Kim, Ludovic Courtès, Maxim Ivanov, Petr Rockai,
Ricardo M. Correia and Shea Levy.
