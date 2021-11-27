R""(

# Description

`nix registry` provides subcommands for managing *flake
registries*. Flake registries are a convenience feature that allows
you to refer to flakes using symbolic identifiers such as `nixpkgs`,
rather than full URLs such as `git://github.com/NixOS/nixpkgs`. You
can use these identifiers on the command line (e.g. when you do `nix
run nixpkgs#hello`) or in flake input specifications in `flake.nix`
files. The latter are automatically resolved to full URLs and recorded
in the flake's `flake.lock` file.

In addition, the flake registry allows you to redirect arbitrary flake
references (e.g. `github:NixOS/patchelf`) to another location, such as
a local fork.

There are multiple registries. These are, in order from lowest to
highest precedence:

* The global registry, which is a file downloaded from the URL
  specified by the setting `flake-registry`. It is cached locally and
  updated automatically when it's older than `tarball-ttl`
  seconds. The default global registry is kept in [a GitHub
  repository](https://github.com/NixOS/flake-registry).

* The system registry, which is shared by all users. The default
  location is `/etc/nix/registry.json`. On NixOS, the system registry
  can be specified using the NixOS option `nix.registry`.

* The user registry `~/.config/nix/registry.json`. This registry can
  be modified by commands such as `nix flake pin`.

* Overrides specified on the command line using the option
  `--override-flake`.

# Registry format

A registry is a JSON file with the following format:

```json
{
  "version": 2,
  "flakes": [
    {
      "from": {
        "type": "indirect",
        "id": "nixpkgs"
      },
      "to": {
        "type": "github",
        "owner": "NixOS",
        "repo": "nixpkgs"
      }
    },
    ...
  ]
}
```

That is, it contains a list of objects with attributes `from` and
`to`, both of which contain a flake reference in attribute
representation. (For example, `{"type": "indirect", "id": "nixpkgs"}`
is the attribute representation of `nixpkgs`, while `{"type":
"github", "owner": "NixOS", "repo": "nixpkgs"}` is the attribute
representation of `github:NixOS/nixpkgs`.)

Given some flake reference *R*, a registry entry is used if its
`from` flake reference *matches* *R*. *R* is then replaced by the
*unification* of the `to` flake reference with *R*.

# Matching

The `from` flake reference in a registry entry *matches* some flake
reference *R* if the attributes in `from` are the same as the
attributes in `R`. For example:

* `nixpkgs` matches with `nixpkgs`.

* `nixpkgs` matches with `nixpkgs/nixos-20.09`.

* `nixpkgs/nixos-20.09` does not match with `nixpkgs`.

* `nixpkgs` does not match with `git://github.com/NixOS/patchelf`.

# Unification

The `to` flake reference in a registry entry is *unified* with some flake
reference *R* by taking `to` and applying the `rev` and `ref`
attributes from *R*, if specified. For example:

* `github:NixOS/nixpkgs` unified with `nixpkgs` produces `github:NixOS/nixpkgs`.

* `github:NixOS/nixpkgs` unified with `nixpkgs/nixos-20.09` produces `github:NixOS/nixpkgs/nixos-20.09`.

* `github:NixOS/nixpkgs/master` unified with `nixpkgs/nixos-20.09` produces `github:NixOS/nixpkgs/nixos-20.09`.

)""
