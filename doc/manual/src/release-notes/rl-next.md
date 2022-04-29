# Release X.Y (202?-??-??)

* Nix now provides better integration with zsh's run-help feature. It is now
  included in the Nix installation in the form of an autoloadable shell
  function, run-help-nix. It picks up Nix subcommands from the currently typed
  in command and directs the user to the associated man pages.

* `nix repl` has a new build-'n-link (`:bl`) command that builds a derivation
  while creating GC root symlinks.

* The path produced by `builtins.toFile` is now allowed to be imported or read
  even with restricted evaluation. Note that this will not work with a
  read-only store.

* `nix build` has a new `--print-out-paths` flag to print the resulting output paths.
  This matches the default behaviour of `nix-build`.

* Error traces have been reworked to provide detailed explanations and more
  accurate error locations. A short excerpt of the trace is now shown by
  default when an error occurs.
