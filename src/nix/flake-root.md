R""(

# Limitations
- This subcommand doesn't support codebases that keep the flake.nix in a subdirectory.

# Examples

* Get the root folder of a codebase with the shell in folder /path/to/folder and flake.nix in /path/to:

```console
/path/to/folder$ nix flake root
/path/to

/path/to/folder$ nix flake root -r
path:/tmp/eoq
```

* Get the root folder of a codebase with the shell in folder /path/to/folder, a flake.nix in /path/to and a git repo initialized

```console
/path/to/folder$ nix flake root
/path/to

/path/to/folder$ nix flake root -r
git+file:///path/to
```

# Description

This command uses the logic used to find flake.nix for commands
such as `nix build` and shows the absolute path, or optionally,
the flake reference.

)""
