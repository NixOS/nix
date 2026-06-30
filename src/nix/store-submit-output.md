R""(

# Examples

* To submit a given path as the output named `out`:

  ```console
  # nix store submit-output /nix/store/h6zs50y2662apmnbcnhnbxll76lv02yy-hello-2.12.3 out
  ```

# Description

`nix store submit-output` registers a path as an output of the currently-running derivation.

It only functions when running inside a content-addressing derivation with the `builder-rpc-v0`
system feature, which provides a limited daemon socket to the builder.
Execution in any other environment will fail.

)""
