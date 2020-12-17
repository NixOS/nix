R""(

# Examples

* To run blender:

```console
# nix run blender-bin
```

* To run vim from nixpkgs:

```console
# nix run nixpkgs#vim
```

* To run vim from nixpkgs with arguments:

```console
# nix run nixpkgs#vim -- --help
```

# Description

`nix run` executes a Nix application. A Nix application is a set with two
entries: `type = "app"` and `program = "<path-to-program>"`. If a derivation is
given as an argument, it is implicitly converted to an app by setting `program`
to `"${path-to-derivation}/bin/${name-of-derivation}"`.

If just a flake is given as an argument, the attribute `defaultApp."<system>"`
of the flake will be executed. If an output is specified (`nix run
<flake-name>#<output-name>`), the attribute `apps."<system>"."<output-name>"`
will be executed.

Flakes can define an app as their
`defaultApp."<system>"`-attribute. [blender-bin](https://github.com/edolstra/nix-warez/blob/master/blender/flake.nix#L148)
does this, for example. A flake can also provide multiple apps by providing the
`apps."<system>"."<app-name>"`-attributes. The user can then

)""
