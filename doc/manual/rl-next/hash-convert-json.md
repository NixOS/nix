---
synopsis: "`nix hash convert` supports JSON format"
prs: []
issues: []
---

`nix hash convert` now supports a `json-base16` format for both input (`--from`) and output (`--to`).

This format represents hashes as structured JSON objects:

```json
{
    "format": "base16",
    "algorithm": "sha256",
    "hash": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
}
```

Currently, the `format` field must always `"base16"` (hexadecimal) for input, and will always be that for  output.

This is used in the new structured JSON outputs for store path info and derivations, and will be used whenever JSON formats needs to contain hashes going forward.

JSON input is also auto-detected when `--from` is not specified.

See [`nix hash convert`](@docroot@/command-ref/new-cli/nix3-hash-convert.md) for usage examples.
