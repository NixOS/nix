R""(

# Examples

* Run the default app from the `blender-bin` flake:

  ```console
  # nix run blender-bin
  ```

* Run a non-default app from the `blender-bin` flake:

  ```console
  # nix run blender-bin#blender_2_83
  ```

  Tip: you can find apps provided by this flake by running `nix flake
  show blender-bin`.

* Run `vim` from the `nixpkgs` flake:

  ```console
  # nix run nixpkgs#vim
  ```

  Note that `vim` (as of the time of writing of this page) is not an
  app but a package. Thus, Nix runs the eponymous file from the `vim`
  package.

* Run `vim` with arguments:

  ```console
  # nix run nixpkgs#vim -- --help
  ```

# Description

`nix run` builds and runs *installable*, which must evaluate to an
*app* or a regular Nix derivation.

If *installable* evaluates to an *app* (see below), it executes the
program specified by the app definition.

If *installable* evaluates to a derivation, it will try to execute the
program `<out>/bin/<name>`, where *out* is the primary output store
path of the derivation, and *name* is the first of the following that
exists:

* The `meta.mainProgram` attribute of the derivation.
* The `pname` attribute of the derivation.
* The name part of the value of the `name` attribute of the derivation.

For instance, if `name` is set to `hello-1.10`, `nix run` will run
`$out/bin/hello`.

# Flake output attributes

If no flake output attribute is given, `nix run` tries the following
flake output attributes:

* `apps.<system>.default`

* `packages.<system>.default`

If an attribute *name* is given, `nix run` tries the following flake
output attributes:

* `apps.<system>.<name>`

* `packages.<system>.<name>`

* `legacyPackages.<system>.<name>`

# Apps

An app is specified by a flake output attribute named
`apps.<system>.<name>`. It looks like this:

```nix
apps.x86_64-linux.blender_2_79 = {
  type = "app";
  program = "${self.packages.x86_64-linux.blender_2_79}/bin/blender";
};
```

The only supported attributes are:

* `type` (required): Must be set to `app`.

* `program` (required): The full path of the executable to run. It
  must reside in the Nix store.

)""
