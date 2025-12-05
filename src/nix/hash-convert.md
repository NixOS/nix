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
  error: input hash 'ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=' has format 'base64', but '--from nix32' was specified

  # nix hash convert --hash-algo sha256 --from nix32 1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s
  sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=
  ```

* Convert a hash to the `json-base16` format:

  ```console
  $ nix hash convert --to json-base16 "sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="
  {
    "algorithm":"sha256",
    "format":"base16",
    "hash":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  }
  ```

* Convert a hash from the `json-base16` format:

  ```console
  $ nix hash convert --from json-base16 '{
    "format": "base16",
    "algorithm": "sha256",
    "hash": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  }'
  sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=
  ```

# Description

`nix hash convert` converts hashes from one encoding to another.

)""
