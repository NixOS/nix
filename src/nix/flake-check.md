R""(

# Examples

* Evaluate the flake in the current directory, and build its checks:

  ```console
  # nix flake check
  ```

* Verify that the `patchelf` flake evaluates, but don't build its
  checks:

  ```console
  # nix flake check --no-build github:NixOS/patchelf
  ```

# Description

This command verifies that the flake specified by flake reference
*flake-url* can be evaluated successfully (as detailed below), and
that the derivations specified by the flake's `checks` output can be
built successfully.

If the `keep-going` option is set to `true`, Nix will keep evaluating as much
as it can and report the errors as it encounters them. Otherwise it will stop
at the first error.

# Evaluation checks

The following flake output attributes must be derivations:

* `checks.`*system*`.`*name*
* `defaultPackage.`*system*
* `devShell.`*system*
* `devShells.`*system*`.`*name*
* `nixosConfigurations.`*name*`.config.system.build.toplevel`
* `packages.`*system*`.`*name*

The following flake output attributes must be [app
definitions](./nix3-run.md):

* `apps.`*system*`.`*name*
* `defaultApp.`*system*

The following flake output attributes must be [template
definitions](./nix3-flake-init.md):

* `defaultTemplate`
* `templates.`*name*

The following flake output attributes must be *Nixpkgs overlays*:

* `overlay`
* `overlays.`*name*

The following flake output attributes must be *NixOS modules*:

* `nixosModule`
* `nixosModules.`*name*

The following flake output attributes must be
[bundlers](./nix3-bundle.md):

* `bundlers.`*name*
* `defaultBundler`

In addition, the `hydraJobs` output is evaluated in the same way as
Hydra's `hydra-eval-jobs` (i.e. as a arbitrarily deeply nested
attribute set of derivations). Similarly, the
`legacyPackages`.*system* output is evaluated like `nix-env -qa`.

# Unchecked attributes

Some attributes do not come with checks but are permitted by `nix flake check`;
they do not cause a warning.

- `lib`: a Nix-language library containing functions and perhaps also constants.
  No convention has been established for `lib`, so its value is not checked.

- `debug`: Anything that the flake author deems useful for troubleshooting the flake.

- `internals`: Anything that the flake needs for its own purposes, such as attributes that support developer tooling.
  Consumers of a flake should avoid this attribute, as its author should feel free to alter its contents to their needs.

A number of well known module system applications' attributes is also ignored.
Configuration values have gained a `_type` tag since NixOS 23.05 and may be checked in the future.
Modules have such a flexible syntax that checking them is barely worthwhile. For now, `nix flake check` does not check them, so that these attributes' interpretation is up to the module system and its applications. Limited checks might be added later.

- `darwinConfigurations`
- `darwinModules`
- `flakeModule`
- `flakeModules`
- `homeConfigurations`
- `nixopsConfigurations`

`nix flake check` also allows for general purpose module system attributes. Module system applications should pick a [*class*](https://nixos.org/manual/nixpkgs/unstable/index.html#module-system-lib-evalModules-param-class) to separate their own modules and configurations from other applications. The subattribute structure below may be subject to change.

- `modules.`*class*`.`*moduleName*: Modules that are compatible with a specific module system application.
- `modules.generic.`*moduleName*: Generic modules that are not tied to an application.
- `configurations.`*class*`.`*name*: Concrete configurations. These tend to correspond to single real world objects, or sometimes classes of objects where the same configuration applies.

Some third party attributes are also ignored.

- `herculesCI`

)""
