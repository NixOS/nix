# Flake Schemas

Flake schemas are a mechanism to allow tools like `nix flake show` and `nix flake check` to enumerate and check the contents of a flake
in a generic way, without requiring built-in knowledge of specific flake output types like `packages` or `nixosConfigurations`.

A flake can define schemas for its outputs by defining a `schemas` output. `schemas` should be an attribute set with an attribute for
every output type that you want to be supported. If a flake does not have a `schemas` attribute, Nix uses a built-in set of schemas (namely https://github.com/DeterminateSystems/flake-schemas).

A schema is an attribute set with the following attributes:

* `version`: Should be set to 1.
* `doc`: A string containing documentation about the flake output type in Markdown format.
* `allowIFD` (defaults to `true`): Whether the evaluation of the output attributes of this flake can read from derivation outputs.
* `inventory`: A function that returns the contents of the flake output (described below).

# Inventory

The `inventory` function returns a *node* describing the contents of the flake output. A node is either a *leaf node* or a *non-leaf node*. This allows nested flake output attributes to be described (e.g. `x86_64-linux.hello` inside a `packages` output).

Non-leaf nodes must have the following attribute:

* `children`: An attribute set of nodes. If this attribute is missing, the attribute if a leaf node.

Leaf nodes can have the following attributes:

* `derivation`: The main derivation of this node, if any. It must evaluate for `nix flake check` and `nix flake show` to succeed.

* `evalChecks`: An attribute set of Boolean values, used by `nix flake check`. Each attribute must evaluate to `true`.

* `isFlakeCheck`: Whether `nix flake check` should build the `derivation` attribute of this node.

* `shortDescription`: A one-sentence description of the node (such as the `meta.description` attribute in Nixpkgs).

* `what`: A brief human-readable string describing the type of the node, e.g. `"package"` or `"development environment"`. This is used by tools like `nix flake show` to describe the contents of a flake.

Both leaf and non-leaf nodes can have the following attributes:

* `forSystems`: A list of Nix system types (e.g. `["x86_64-linux"]`) supported by this node. This is used by tools to skip nodes that cannot be built on the user's system. Setting this on a non-leaf node allows all the children to be skipped, regardless of the `forSystems` attributes of the children. If this attribute is not set, the node is never skipped.

# Example

Here is a schema that checks that every element of the `nixosConfigurations` flake output evaluates and builds correctly (meaning that it has a `config.system.build.toplevel` attribute that yields a buildable derivation).

```
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
          derivation = machine.config.system.build.toplevel;
        }) output;
    };
  };
};
```
