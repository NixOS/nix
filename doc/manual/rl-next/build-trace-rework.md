---
synopsis: "Content-addressed derivations: realisations keyed by store path instead of hash modulo"
issues: [11897]
prs: [12464]
---

The experimental content-addressed (CA) derivation feature has undergone a significant change to how build traces (formerly called "realisations") are identified. This affects the **binary cache protocol** and the **wire protocols**.

### What changed

Previously, a build trace entry (realisation) was keyed by the **hash modulo** of the derivation.
A SHA-256 hash computed via the complex "derivation hash modulo" algorithm.
This required implementations to understand ATerm serialisation and the full derivation hashing scheme just to look up or store build results.

Now, build trace entries are keyed by the **regular derivation store path** plus the output name. For example, instead of:

```
sha256:ba7816bf8f01...!out
```

The key is now:

```
/nix/store/abc...-foo.drv^out
```

This is simpler, more intuitive, and means that third-party tools implementing CA derivation support (e.g., Hydra)
no longer need to implement the derivation hash modulo algorithm.

### Binary cache protocol

- The directory for build traces moved from `realisations/` to `build-trace-v2/`.
- File paths changed from `realisations/<hash>!<output>.doi` to `build-trace-v2/<drvName>/<outputName>.doi`.
- The JSON format of build trace entries is now split into `key` and `value` objects:
  ```json
  {
    "key": {
      "drvPath": "abc...-foo.drv",
      "outputName": "out"
    },
    "value": {
      "outPath": "xyz...-foo",
      "signatures": ["..."]
    }
  }
  ```
  Previously, these were flat objects with a string `id` field like `"sha256:...!out"`.
- The deprecated `dependentRealisations` field has been removed.

Existing binary caches will need to be re-populated with the new format for CA derivation build traces.
Old build traces at the previous URLs are simply abandoned.
Non-CA builds are unaffected.

### Wire protocols

- **Worker protocol**:
  A new feature flag `realisation-with-path-not-hash` is negotiated during the handshake.
  Clients and daemons that both support this feature use the new binary serialisation for `DrvOutput`, `UnkeyedRealisation`, and related types.
  Fallback to older protocol versions gracefully degrades (realisations are unavailable).
- **Serve protocol**:
  Bumped from 2.7 to 2.8 with native serialisers for the new types.
  Fallback to older protocol versions gracefully degrades in the same way.

Stable code paths do use the realization fields (`BuildResult::Success::builtOutputs`), but only the output name and outpath parts of that.
For older protocols, we can fake enough of the realisation format to provide those two parts forthat map, which keeps operations like `--print-output-paths` working.

### Impact

- **Non-CA derivation users**: No impact. This only affects the experimental `ca-derivations` feature.
- **Binary cache operators**:
  Binary caches serving CA derivation build traces will need to be repopulated.
  Existing NARs and narinfo files are unaffected.
- **Tool authors**:
  Implementations interfacing with the CA derivations protocol are simplified.
  The derivation hash modulo algorithm is no longer required to form build trace keys.
