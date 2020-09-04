# Release 0.9 (2005-09-16)

NOTE: this version of Nix uses Berkeley DB 4.3 instead of 4.2. The
database is upgraded automatically, but you should be careful not to use
old versions of Nix that still use Berkeley DB 4.2. In particular, if
you use a Nix installed through Nix, you should run

    $ nix-store --clear-substitutes

first.

  - Unpacking of patch sequences is much faster now since we no longer
    do redundant unpacking and repacking of intermediate paths.

  - Nix now uses Berkeley DB 4.3.

  - The `derivation` primitive is lazier. Attributes of dependent
    derivations can mutually refer to each other (as long as there are
    no data dependencies on the `outPath` and `drvPath` attributes
    computed by `derivation`).
    
    For example, the expression `derivation
            attrs` now evaluates to (essentially)
    
        attrs // {
          type = "derivation";
          outPath = derivation! attrs;
          drvPath = derivation! attrs;
        }
    
    where `derivation!` is a primop that does the actual derivation
    instantiation (i.e., it does what `derivation` used to do). The
    advantage is that it allows commands such as `nix-env -qa` and
    `nix-env -i` to be much faster since they no longer need to
    instantiate all derivations, just the `name` attribute.
    
    Also, it allows derivations to cyclically reference each other, for
    example,
    
        webServer = derivation {
          ...
          hostName = "svn.cs.uu.nl";
          services = [svnService];
        };
         
        svnService = derivation {
          ...
          hostName = webServer.hostName;
        };
    
    Previously, this would yield a black hole (infinite recursion).

  - `nix-build` now defaults to using `./default.nix` if no Nix
    expression is specified.

  - `nix-instantiate`, when applied to a Nix expression that evaluates
    to a function, will call the function automatically if all its
    arguments have defaults.

  - Nix now uses libtool to build dynamic libraries. This reduces the
    size of executables.

  - A new list concatenation operator `++`. For example, `[1 2 3] ++
    [4 5
            6]` evaluates to `[1 2 3 4 5
            6]`.

  - Some currently undocumented primops to support low-level build
    management using Nix (i.e., using Nix as a Make replacement). See
    the commit messages for `r3578` and `r3580`.

  - Various bug fixes and performance improvements.
