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
*flake-url* can be evaluated and built successfully according to its
`schemas` flake output. For every flake output that has a schema
definition, `nix flake check` uses the schema to extract the contents
of the output. Then, for every item in the contents:

* It evaluates the elements of the `evalChecks` attribute set returned
  by the schema for that item, printing an error or warning for every
  check that fails to evaluate or that evaluates to `false`.

* It builds `derivation` attribute returned by the schema for that
  item, if the item has the `isFlakeCheck` attribute.

If the `keep-going` option is set to `true`, Nix will keep evaluating as much
as it can and report the errors as it encounters them. Otherwise it will stop
at the first error.

)""
