# Release 1.6 (2013-09-10)

In addition to the usual bug fixes, this release has several new
features:

  - The command `nix-build --run-env` has been renamed to `nix-shell`.

  - `nix-shell` now sources `$stdenv/setup` *inside* the interactive
    shell, rather than in a parent shell. This ensures that shell
    functions defined by `stdenv` can be used in the interactive shell.

  - `nix-shell` has a new flag `--pure` to clear the environment, so you
    get an environment that more closely corresponds to the “real” Nix
    build.

  - `nix-shell` now sets the shell prompt (`PS1`) to ensure that Nix
    shells are distinguishable from your regular shells.

  - `nix-env` no longer requires a `*` argument to match all packages,
    so `nix-env -qa` is equivalent to `nix-env
                    -qa '*'`.

  - `nix-env -i` has a new flag `--remove-all` (`-r`) to remove all
    previous packages from the profile. This makes it easier to do
    declarative package management similar to NixOS’s
    `environment.systemPackages`. For instance, if you have a
    specification `my-packages.nix` like this:
    
        with import <nixpkgs> {};
        [ thunderbird
          geeqie
          ...
        ]
    
    then after any change to this file, you can run:
    
        $ nix-env -f my-packages.nix -ir
    
    to update your profile to match the specification.

  - The ‘`with`’ language construct is now more lazy. It only evaluates
    its argument if a variable might actually refer to an attribute in
    the argument. For instance, this now works:
    
        let
          pkgs = with pkgs; { foo = "old"; bar = foo; } // overrides;
          overrides = { foo = "new"; };
        in pkgs.bar
    
    This evaluates to `"new"`, while previously it gave an “infinite
    recursion” error.

  - Nix now has proper integer arithmetic operators. For instance, you
    can write `x + y` instead of `builtins.add x y`, or `x <
                    y` instead of `builtins.lessThan x y`. The comparison operators also
    work on strings.

  - On 64-bit systems, Nix integers are now 64 bits rather than 32 bits.

  - When using the Nix daemon, the `nix-daemon` worker process now runs
    on the same CPU as the client, on systems that support setting CPU
    affinity. This gives a significant speedup on some systems.

  - If a stack overflow occurs in the Nix evaluator, you now get a
    proper error message (rather than “Segmentation fault”) on some
    systems.

  - In addition to directories, you can now bind-mount regular files in
    chroots through the (now misnamed) option `build-chroot-dirs`.

This release has contributions from Domen Kožar, Eelco Dolstra, Florian
Friesdorf, Gergely Risko, Ivan Kozik, Ludovic Courtès and Shea Levy.
