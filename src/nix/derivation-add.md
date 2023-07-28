R""(

# Examples

* Rountrip export and import of a derivation (basically a no-op):

  ```console
  nix derivation show nixpkgs#hello | nix derivation add
  /nix/store/8rgfc52k0529kypam0jy5p1a4jsj4dbq-hello-2.12.1.drv
  ```

* Move a package from one machine to another without transferring the output paths:

  This example serves as an illustration. It is very manual, and will only work
  if all input and output paths can be substituted on the target machine.
  Use [`nix copy`] instead for real-life scenarios.

  ```console
  # nix derivation show -r /nix/store/d26w2dip6kcjpw6dfcrjmpkprrabjz60-hello-2.12.1 > hello.drv.json
  ```

  Transfer the file via any means conceivable to the target machine and continue there:

  ```console
  # nix derivation add < hello.drv.json
  /nix/store/8rgfc52k0529kypam0jy5p1a4jsj4dbq-hello-2.12.1.drv
  # nix build /nix/store/8rgfc52k0529kypam0jy5p1a4jsj4dbq-hello-2.12.1.drv^out
  # readlink result
  /nix/store/d26w2dip6kcjpw6dfcrjmpkprrabjz60-hello-2.12.1
  ```

  Note that the final output path is equal to the one that we started with.


[`nix copy`]: ./nix3-copy.md

# Description

This command reads from standard input a JSON representation of a single
[store derivation] to which an [*installable*](./nix.md#installables) evaluates
or an object containing multiple derivations as output by [`derivation show`].

Store derivations are used internally by Nix. They are store paths with
extension `.drv` that represent the build-time dependency graph to which
a Nix expression evaluates.

[store derivation]: ../../glossary.md#gloss-store-derivation

The JSON format is documented under the [`derivation show`] command.

In addition, a single derivation is not required to be wrapped as
shown below, though this is how `derivation show` will output it.

```json
{
  "/nix/store/s6rn4jz1sin56rf4qj5b5v8jxjm32hlk-hello-2.10.drv": {
    {
      "name": "hello-2.10",
      …
    }
  }
}
```

Instead, a single unwrapped derivation can also be passed:

```json
{
  "name": "hello-2.10",
  …
}
```

Wrapping *is* required to pass multiple derivations, however.

[`derivation show`]: ./nix3-derivation-show.md

)""
