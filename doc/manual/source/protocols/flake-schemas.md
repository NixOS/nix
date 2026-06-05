# Flake Schemas

Flake schemas are a mechanism to allow tools like `nix flake show` and `nix flake check` to enumerate and check the contents of a flake
in a generic way, without requiring built-in knowledge of specific flake output types like `packages` or `nixosConfigurations`.

A flake can define schemas for its outputs by defining a `schemas` output. `schemas` should be an attribute set with an attribute for
every output type that you want to be supported. If a flake does not have a `schemas` attribute, Nix uses a built-in set of schemas (namely https://github.com/DeterminateSystems/flake-schemas).

A schema is an attribute set with the following attributes:

| Attribute         | Description                                                                                                                  | Default |
| :---------------- | :----------------------------------------------------------------------------------------------------------------------------| :------ |
| `version`         | Should be set to 1                                                                                                           |         |
| `doc`             | A string containing documentation about the flake output type in Markdown format.                                            |         |
| `allowIFD`        | Whether the evaluation of the output attributes of this flake can read from derivation outputs.                              | `true`  |
| `inventory`       | A function that returns the contents of the flake output (described [below](#inventory)).                                    |         |
| `roles`           | The roles supported by this flake output type (see [below](#roles)).                                                         |         |
| `appendSystem`    | Whether the current system type is appended to the flake output attribute path, as in outputs like `packages`.               |         |
| `defaultAttrPath` | A default flake output attribute path suffix. For example, `packages` will look for `default` if no attribute path is given. |         |

# Inventory

The `inventory` function returns a _node_ describing the contents of the flake output. A node is either a _leaf node_ or a _non-leaf node_. This allows nested flake output attributes to be described (e.g. `x86_64-linux.hello` inside a `packages` output).

Non-leaf nodes must have the following attribute:

| Attribute  | Description                                                                            |
| :--------- | :------------------------------------------------------------------------------------- |
| `children` | An attribute set of nodes. If this attribute is missing, the attribute is a leaf node. |

Leaf nodes can have the following attributes:

| Attribute            | Description                                                                                                                                                                                      |
| :------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `derivationAttrPath` | If not null, a list of strings denoting the attribute path of the "main" derivation of this node.                                                                                                |
| `evalChecks`         | An attribute set of Boolean values, used by `nix flake check`. Each attribute must evaluate to `true`.                                                                                           |
| `isFlakeCheck`       | Whether `nix flake check` should build the attribute denoted by `derivationAttrPath`.                                                                                                            |
| `shortDescription`   | A one-sentence description of the node (such as the `meta.description` attribute in Nixpkgs).                                                                                                    |
| `what`               | A brief human-readable string describing the type of the node, e.g. `"package"` or `"development environment"`. This is used by tools like `nix flake show` to describe the contents of a flake. |

Both leaf and non-leaf nodes can have the following attributes:

| Attribute    | Description                                                                                                                                                                                                                                                                                                                                            |
| :----------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `forSystems` | A list of Nix system types (e.g. `["x86_64-linux"]`) supported by this node. This is used by tools to skip nodes that cannot be built on the user's system. Setting this on a non-leaf node allows all the children to be skipped, regardless of the `forSystems` attributes of the children. If this attribute is not set, the node is never skipped. |
| `isLegacy`   | If set to true, this node is skipped unless the `--legacy` CLI flag is set. |

# Roles

Roles allow schemas to declare what commands operate on them. For instance, to have the `nix build` command build a flake output, the schema should declare:
```nix
roles.nix-build = { };
```

The following roles are used by various Nix commands:

* `nix-build`: Used by `nix build`, `nix shell`, and `nix develop`.
* `nix-bundler`: Denotes bundler functions used by `nix bundle`.
* `nix-develop`: Used by `nix develop`.
* `nix-fmt`: Used by `nix formatter`.
* `nix-run`: Used by `nix run` and `nix bundle`.
* `nix-search`: Denotes an output that will be searched by `nix search`.
* `nix-template`: Used by `nix flake init` and `nix flake new`.

Tools are free to define new roles. For instance, instead of hard-coding a flake output type like `nixosConfigurations`, `nixos-rebuild --flake` could use any flake output that implements a `nixos-configuration` role.

# Example

Here is a schema that checks that every element of the `nixosConfigurations` flake output evaluates and builds correctly (meaning that it has a `config.system.build.toplevel` attribute that yields a buildable derivation).

```nix
outputs = {
  schemas.nixosConfigurations = {
    version = 1;
    doc = ''
      The `nixosConfigurations` flake output defines NixOS system configurations.
    '';
    inventory = output: {
      children = builtins.mapAttrs (configName: machine:
        {
          what = "NixOS configuration";
          derivationAttrPath = [ "config" "system" "build" "toplevel" ];
        }) output;
    };
  };
};
```
