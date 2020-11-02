# Release 0.6 (2004-11-14)

  - Rewrite of the normalisation engine.
    
      - Multiple builds can now be performed in parallel (option `-j`).
    
      - Distributed builds. Nix can now call a shell script to forward
        builds to Nix installations on remote machines, which may or may
        not be of the same platform type.
    
      - Option `--fallback` allows recovery from broken substitutes.
    
      - Option `--keep-going` causes building of other (unaffected)
        derivations to continue if one failed.

  - Improvements to the garbage collector (i.e., it should actually work
    now).

  - Setuid Nix installations allow a Nix store to be shared among
    multiple users.

  - Substitute registration is much faster now.

  - A utility `nix-build` to build a Nix expression and create a symlink
    to the result int the current directory; useful for testing Nix
    derivations.

  - Manual updates.

  - `nix-env` changes:
    
      - Derivations for other platforms are filtered out (which can be
        overridden using `--system-filter`).
    
      - `--install` by default now uninstall previous derivations with
        the same name.
    
      - `--upgrade` allows upgrading to a specific version.
    
      - New operation `--delete-generations` to remove profile
        generations (necessary for effective garbage collection).
    
      - Nicer output (sorted, columnised).

  - More sensible verbosity levels all around (builder output is now
    shown always, unless `-Q` is given).

  - Nix expression language changes:
    
      - New language construct: `with
                                                        E1;
                                                        E2` brings all attributes defined in the attribute set *E1* in
        scope in *E2*.
    
      - Added a `map` function.
    
      - Various new operators (e.g., string concatenation).

  - Expression evaluation is much faster.

  - An Emacs mode for editing Nix expressions (with syntax highlighting
    and indentation) has been added.

  - Many bug fixes.
