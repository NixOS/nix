---
synopsis: "JSON format changes for store path info and derivations"
prs: []
issues: []
---

JSON formats for store path info and derivations have been updated with new versions and structured fields.

## Store Path Info JSON

`nix path-info --json` now requires a `--json-format` flag to specify the output format version.
Using `--json` without `--json-format` is deprecated and will become an error in a future release.
For now, it defaults to version 1 with a warning, for a smoother migration.

### Version 1 (`--json-format 1`)

This is the legacy format, preserved for backwards compatibility:

- String-based hash values (e.g., `"narHash": "sha256:FePFYIlM..."`)
- String-based content addresses (e.g., `"ca": "fixed:r:sha256:1abc..."`)
- Full store paths for map keys and references (e.g., `"/nix/store/abc...-foo"`)
- Now includes `"storeDir"` field at the top level

### Version 2 (`--json-format 2`)

The new structured format follows the [JSON guidelines](@docroot@/development/json-guideline.md) with the following changes:

- **Nested structure with top-level metadata**:

  The output is now wrapped in an object with `version`, `storeDir`, and `info` fields:

  ```json
  {
    "version": 2,
    "storeDir": "/nix/store",
    "info": { ... }
  }
  ```

  The map from store bath base names to store object info is nested under the `info` field.

- **Store path base names instead of full paths**:

  Map keys and references use store path base names (e.g., `"abc...-foo"`) instead of full absolute store paths.
  Combined with `storeDir`, the full path can be reconstructed.

- **Structured `ca` field**:

  Content address is now a structured JSON object instead of a string:

  - Old: `"ca": "fixed:r:sha256:1abc..."`
  - New: `"ca": {"method": "nar", "hash": {"algorithm": "sha256", "format": "base16", "hash": "10c209fa..."}}`
  - Still `null` values for input-addressed store objects

- **Structured hash fields**:

  Hash values (`narHash` and `downloadHash`) are now structured JSON objects instead of strings:

  - Old: `"narHash": "sha256:FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="`
  - New: `"narHash": {"algorithm": "sha256", "format": "base16", "hash": "15e3c5608946..."}`
  - Same structure applies to `downloadHash` in NAR info contexts
  - The `format` field is always `"base16"` (hexadecimal)

Nix currently only produces, and doesn't consume this format.

Additionally the following field is added to both formats.
(The `version` tracks breaking changes, and adding fields to outputted JSON is not a breaking change.)

- **`version` field**:

  All store path info JSON now includes `"version": <1|2>`.

- **`storeDir` field**:

  Top-level `"storeDir"` field contains the store directory path (e.g., `"/nix/store"`).

## Derivation JSON (Version 4)

The derivation JSON format has been updated from version 3 to version 4:

- **Restructured inputs**:

  Inputs are now nested under an `inputs` object:

  - Old: `"inputSrcs": [...], "inputDrvs": {...}`
  - New: `"inputs": {"srcs": [...], "drvs": {...}}`

- **Consistent content addresses**:

  Fixed content-addressed outputs now use structured JSON format.
  This is the same format as `ca` in store path info (after the new version).

Version 3 and earlier formats are *not* accepted when reading.

**Affected command**: `nix derivation`, namely its `show` and `add` sub-commands.
