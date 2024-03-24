R""(

# Examples

* Convert a hash to `nix32` (a base-32 encoding with a Nix-specific character set).

  ```console
  $ nix hash convert --hash-algo sha1 --to nix32 800d59cfcd3c05e900cb4e214be48f6b886a08df
  vw46m23bizj4n8afrc0fj19wrp7mj3c0
  ```

* Convert a hash to [the `sri` format](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) that includes an algorithm specification:

  ```console
  # nix hash convert --hash-algo sha1 800d59cfcd3c05e900cb4e214be48f6b886a08df
  sha1-gA1Zz808BekAy04hS+SPa4hqCN8=
  ```

  or with an explicit `--to` format:

  ```console
  # nix hash convert --hash-algo sha1 --to sri 800d59cfcd3c05e900cb4e214be48f6b886a08df
  sha1-gA1Zz808BekAy04hS+SPa4hqCN8=
  ```

* Assert the input format of the hash:

  ```console
  # nix hash convert --hash-algo sha256 --from nix32 ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=
  error: input hash 'ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=' does not have the expected format '--from nix32'

  # nix hash convert --hash-algo sha256 --from nix32 1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s
  sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=
  ```

# Description

`nix hash convert` converts hashes from one encoding to another.

)""
