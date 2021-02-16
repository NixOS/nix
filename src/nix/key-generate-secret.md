R""(

# Examples

* Generate a new secret key:

  ```console
  # nix key generate-secret --key-name cache.example.org-1 > ./secret-key
  ```

  We can then use this key to sign the closure of the Hello package:

  ```console
  # nix build nixpkgs#hello
  # nix store sign --key-file ./secret-key --recursive ./result
  ```

  Finally, we can verify the store paths using the corresponding
  public key:

  ```
  # nix store verify --trusted-public-keys $(nix key convert-secret-to-public < ./secret-key) ./result
  ```

# Description

This command generates a new Ed25519 secret key for signing store
paths and prints it on standard output. Use `nix key
convert-secret-to-public` to get the corresponding public key for
verifying signed store paths.

The mandatory argument `--key-name` specifies a key name (such as
`cache.example.org-1). It is used to look up keys on the client when
it verifies signatures. It can be anything, but it’s suggested to use
the host name of your cache (e.g.  `cache.example.org`) with a suffix
denoting the number of the key (to be incremented every time you need
to revoke a key).

# Format

Both secret and public keys are represented as the key name followed
by a base-64 encoding of the Ed25519 key data, e.g.

```
cache.example.org-0:E7lAO+MsPwTFfPXsdPtW8GKui/5ho4KQHVcAGnX+Tti1V4dUxoVoqLyWJ4YESuZJwQ67GVIksDt47og+tPVUZw==
```

)""
