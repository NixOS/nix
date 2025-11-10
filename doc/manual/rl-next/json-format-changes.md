---
synopsis: "JSON format changes for store path info and derivations"
prs: []
issues: []
---

JSON formats for store path info and derivations have been updated with new versions and structured fields.

## Store Path Info JSON (Version 2)

The store path info JSON format has been updated from version 1 to version 2:

- **Added `version` field**:

  All store path info JSON now includes `"version": 2`.

- **Structured `ca` field**:

  Content address is now a structured JSON object instead of a string:

  - Old: `"ca": "fixed:r:sha256:1abc..."`
  - New: `"ca": {"method": "nar", "hash": {"algorithm": "sha256", "format": "base64", "hash": "EMIJ+giQ..."}}`
  - Still `null` values for input-addressed store objects

- **Structured hash fields**:

  Hash values (`narHash` and `downloadHash`) are now structured JSON objects instead of strings:

  - Old: `"narHash": "sha256:FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="`
  - New: `"narHash": {"algorithm": "sha256", "format": "base64", "hash": "FePFYIlM..."}`
  - Same structure applies to `downloadHash` in NAR info contexts

Nix currently only produces, and doesn't consume this format.

**Affected command**: `nix path-info --json`

## Derivation JSON (Version 4)

The derivation JSON format has been updated from version 3 to version 4:

- **Restructured inputs**:

  Inputs are now nested under an `inputs` object:

  - Old: `"inputSrcs": [...], "inputDrvs": {...}`
  - New: `"inputs": {"srcs": [...], "drvs": {...}}`

- **Consistent content addresses**:

  Floating content-addressed outputs now use structured JSON format.
  This is the same format as `ca` in in store path info (after the new version).

Version 3 and earlier formats are *not* accepted when reading.

**Affected command**: `nix derivation`, namely it's `show` and `add` sub-commands.
