---
synopsis: "`nix search` can now calculate paths, check cache availability, transform output, and stream JSON"
---

`nix search --json` now supports four new flags:

- `--calc-derivation`: Includes both the output path and derivation path of each matching derivation in the JSON output. This will instantiate the derivations by writing .drv files to the store.
- `--check-cache`: Adds a `cached` boolean field indicating whether the output path is available in a binary cache, without downloading it. Implies `--json`.
- `--apply <expr>`: Apply a Nix function to each matching derivation to transform the output. Implies `--json`.
- `--json-lines`: Outputs results in JSON Lines (JSONL) format, with one result per line. This enables streaming output as results are found.

Example usage:

```console
# Include paths in output
$ nix search nixpkgs hello --json --calc-derivation
{
  "legacyPackages.x86_64-linux.hello": {
    "description": "Program that produces a familiar, friendly greeting",
    "drvPath": "/nix/store/...-hello-2.12.3.drv",
    "outPath": "/nix/store/10s5j3mfdg22k1597x580qrhprnzcjwb-hello-2.12.3",
    "pname": "hello",
    "version": "2.12.3"
  }
}

# Check if packages are cached
$ nix search nixpkgs hello --json --check-cache
{
  "legacyPackages.x86_64-linux.hello": {
    "cached": true,
    "description": "Program that produces a familiar, friendly greeting",
    "pname": "hello",
    "version": "2.12.3"
  }
}

# Transform output with --apply
$ nix search nixpkgs hello --apply 'drv: { name = drv.pname; out = drv.outPath; }'
{
  "legacyPackages.x86_64-linux.hello": {
    "name": "hello",
    "out": "/nix/store/10s5j3mfdg22k1597x580qrhprnzcjwb-hello-2.12.3"
  }
}

# Streaming output with JSON Lines
$ nix search nixpkgs hello --json-lines --calc-derivation
{"legacyPackages.x86_64-linux.hello":{"description":"Program that produces a familiar, friendly greeting","drvPath":"/nix/store/...-hello-2.12.3.drv","outPath":"/nix/store/10s5j3mfdg22k1597x580qrhprnzcjwb-hello-2.12.3","pname":"hello","version":"2.12.3"}}
```
