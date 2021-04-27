R""(

# Examples

* Convert a secret key to a public key:

  ```console
  # echo cache.example.org-0:E7lAO+MsPwTFfPXsdPtW8GKui/5ho4KQHVcAGnX+Tti1V4dUxoVoqLyWJ4YESuZJwQ67GVIksDt47og+tPVUZw== \
    | nix key convert-secret-to-public
  cache.example.org-0:tVeHVMaFaKi8lieGBErmScEOuxlSJLA7eO6IPrT1VGc=
  ```

# Description

This command reads a Ed25519 secret key from standard input, and
writes the corresponding public key to standard output. For more
details, see [nix key generate-secret](./nix3-key-generate-secret.md).

)""
