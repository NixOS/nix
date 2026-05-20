R""(

# Examples

* Convert a Nix public key to PEM:

  ```console
  # nix key convert-secret-to-public < secret-key \
    | nix key convert-public-to-pem
  -----BEGIN PUBLIC KEY-----
  …
  -----END PUBLIC KEY-----
  ```

* Convert a public key to PEM and decode it using OpenSSL:

  ```console
  # nix key convert-public-to-pem < public-key \
    | openssl pkey -pubin -text -noout
  ML-DSA-87 Public-Key:
  pub:
  …
  ```

# Description

This command reads a Nix public verification key (as produced by `nix key convert-secret-to-public`) from standard input and writes the corresponding PEM `SubjectPublicKeyInfo` to standard output. The key name is not included in the PEM output.

)""
