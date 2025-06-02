R""(

# Examples

* To copy signatures from a binary cache to the local store:

  ```console
  # nix store copy-sigs --substituter https://cache.nixos.org \
      --recursive /nix/store/y1x7ng5bmc9s8lqrf98brcpk1a7lbcl5-hello-2.12.1
  ```

* To copy signatures from one binary cache to another:

  ```console
  # nix store copy-sigs --substituter https://cache.nixos.org \
      --store file:///tmp/binary-cache \
      --recursive -v \
      /nix/store/y1x7ng5bmc9s8lqrf98brcpk1a7lbcl5-hello-2.12.1
  imported 2 signatures
  ```

# Description

`nix store copy-sigs` copies store path signatures from one store to another.

It is not advised to copy signatures to binary cache stores. Binary cache signatures are stored in `.narinfo` files. Since these are cached aggressively, clients may not see the new signatures quickly. It is therefore better to set any required signatures when the paths are first uploaded to the binary cache.

Store paths are processed in parallel. The amount of parallelism is controlled by the [`http-connections`](@docroot@/command-ref/conf-file.md#conf-http-connections) settings.

)""
