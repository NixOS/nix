---
synopsis: Add `nix hash convert`
prs: 9452
---

New [`nix hash convert`](https://github.com/NixOS/nix/issues/8876) sub command with a fast track
to stabilization! Examples:

- Convert the hash to `nix32`.

  ```bash
  $ nix hash convert --algo "sha1" --to nix32 "800d59cfcd3c05e900cb4e214be48f6b886a08df"
  vw46m23bizj4n8afrc0fj19wrp7mj3c0
  ```
  `nix32` is a base32 encoding with a nix-specific character set.
  Explicitly specify the hashing algorithm (optional with SRI hashes) but detect hash format by the length of the input
  hash.
- Convert the hash to the `sri` format that includes an algorithm specification:
  ```bash
  nix hash convert --algo "sha1" "800d59cfcd3c05e900cb4e214be48f6b886a08df"
  sha1-gA1Zz808BekAy04hS+SPa4hqCN8=
  ```
  or with an explicit `-to` format:
  ```bash
  nix hash convert --algo "sha1" --to sri "800d59cfcd3c05e900cb4e214be48f6b886a08df"
  sha1-gA1Zz808BekAy04hS+SPa4hqCN8=
  ```
- Assert the input format of the hash:
  ```bash
  nix hash convert --algo "sha256" --from nix32 "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="
  error: input hash 'ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=' does not have the expected format '--from nix32'
  nix hash convert --algo "sha256" --from nix32 "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s"
  sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=
  ```

The `--to`/`--from`/`--algo` parameters have context-sensitive auto-completion.

## Related Deprecations

The following commands are still available but will emit a deprecation warning. Please convert your code to
`nix hash convert`:

- `nix hash to-base16 $hash1 $hash2`: Use `nix hash convert --to base16 $hash1 $hash2` instead.
- `nix hash to-base32 $hash1 $hash2`: Use `nix hash convert --to nix32 $hash1 $hash2` instead.
- `nix hash to-base64 $hash1 $hash2`: Use `nix hash convert --to base64 $hash1 $hash2` instead.
- `nix hash to-sri $hash1 $hash2`: : Use `nix hash convert --to sri $hash1 $hash2`
  or even just `nix hash convert $hash1 $hash2` instead.
