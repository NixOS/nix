# Release 2.33.0 (2025-12-09)

## New features

- New command `nix registry resolve` [#14595](https://github.com/NixOS/nix/pull/14595)

  This command looks up a flake registry input name and returns the flakeref it resolves to.

  For example, looking up Nixpkgs:

  ```
  $ nix registry resolve nixpkgs
  github:NixOS/nixpkgs/nixpkgs-unstable
  ```

  Upstreamed from [Determinate Nix 3.14.0](https://github.com/DeterminateSystems/nix-src/pull/273).

- `nix flake clone` supports all input types [#14581](https://github.com/NixOS/nix/pull/14581)

  `nix flake clone` now supports arbitrary input types. In particular, this allows you to clone tarball flakes, such as flakes on FlakeHub.

  Upstreamed from [Determinate Nix 3.12.0](https://github.com/DeterminateSystems/nix-src/pull/229).

## Performance improvements

- Git fetcher computes `revCount`s using multiple threads [#14462](https://github.com/NixOS/nix/pull/14462)

  When using Git repositories with a long history, calculating the `revCount` attribute can take a long time. Nix now computes `revCount` using multiple threads, making it much faster (e.g. 9.1s to 3.7s for Nixpkgs).

  Note that if you don't need `revCount`, you can disable it altogether by setting the flake input attribute `shallow = true`.

  Upstreamed from [Determinate Nix 3.12.2](https://github.com/DeterminateSystems/nix-src/pull/245).

- `builtins.stringLength` now runs in constant time [#14442](https://github.com/NixOS/nix/pull/14442)

  The internal representation of strings has been replaced with a size-prefixed Pascal style string. Previously Nix stored strings as a NUL-terminated array of bytes, necessitating a linear scan to calculate the length.

- Uploads to `http://` and `https://` binary cache stores now run in constant memory [#14390](https://github.com/NixOS/nix/pull/14390)

  Nix used to buffer the whole compressed NAR contents in memory. It now reads it in a streaming fashion.

- Channel URLs migrated to channels.nixos.org subdomain [#14517](https://github.com/NixOS/nix/issues/14517) [#14518](https://github.com/NixOS/nix/pull/14518)

  Channel URLs have been updated from `https://nixos.org/channels/` to `https://channels.nixos.org/` throughout Nix. This subdomain provides better reliability with IPv6 support and improved CDN distribution. The old domain apex (`nixos.org/channels/`) currently redirects to the new location but may be deprecated in the future.

- Fix `download buffer is full; consider increasing the 'download-buffer-size' setting` warning [#11728](https://github.com/NixOS/nix/issues/11728) [#14614](https://github.com/NixOS/nix/pull/14614)

  The underlying issue that led to [#11728](https://github.com/NixOS/nix/issues/11728) has been resolved by utilizing
  [libcurl write pausing functionality](https://curl.se/libcurl/c/curl_easy_pause.html) to control backpressure when unpacking to slow destinations like the git-backed tarball cache. The default value of `download-buffer-size` is now 1 MiB and it's no longer recommended to increase it, since the root cause has been fixed.

  This is expected to improve download performance on fast connections, since previously a single slow download consumer would stall the thread and prevent any other transfers from progressing.

  Many thanks go out to the [Lix project](https://lix.systems/) for the [implementation](https://git.lix.systems/lix-project/lix/commit/4ae6fb5a8f0d456b8d2ba2aaca3712b4e49057fc) that served as inspiration for this change and for triaging libcurl [issues with pausing](https://github.com/curl/curl/issues/19334).

- Significantly improve tarball unpacking performance [#14689](https://github.com/NixOS/nix/pull/14689) [#14696](https://github.com/NixOS/nix/pull/14696) [#10683](https://github.com/NixOS/nix/issues/10683) [#11098](https://github.com/NixOS/nix/issues/11098)

  Nix uses a content-addressed cache backed by libgit2 for deduplicating files fetched via `fetchTarball` and `github`, `tarball` flake inputs. Its usage has been significantly optimised to reduce the amount of I/O operations that are performed. For a typical nixpkgs source tarball this results in 200 times fewer system calls on Linux. In combination with libcurl pausing this alleviates performance regressions stemming from the tarball cache.

- Already valid derivations are no longer copied to the store [#14219](https://github.com/NixOS/nix/pull/14219)

  This results in a modest speedup when using the Nix daemon.

- `nix nar ls` and `nix nar cat` are significantly faster and no longer buffer the whole NAR in memory [#14273](https://github.com/NixOS/nix/pull/14273) [#14732](https://github.com/NixOS/nix/pull/14732)

## S3 improvements

- Improved S3 binary cache support via HTTP [#11748](https://github.com/NixOS/nix/issues/11748) [#12403](https://github.com/NixOS/nix/issues/12403) [#12671](https://github.com/NixOS/nix/issues/12671) [#13084](https://github.com/NixOS/nix/issues/13084) [#13752](https://github.com/NixOS/nix/pull/13752) [#13823](https://github.com/NixOS/nix/pull/13823) [#14026](https://github.com/NixOS/nix/pull/14026) [#14120](https://github.com/NixOS/nix/pull/14120) [#14131](https://github.com/NixOS/nix/pull/14131) [#14135](https://github.com/NixOS/nix/pull/14135) [#14144](https://github.com/NixOS/nix/pull/14144) [#14170](https://github.com/NixOS/nix/pull/14170) [#14190](https://github.com/NixOS/nix/pull/14190) [#14198](https://github.com/NixOS/nix/pull/14198) [#14206](https://github.com/NixOS/nix/pull/14206) [#14209](https://github.com/NixOS/nix/pull/14209) [#14222](https://github.com/NixOS/nix/pull/14222) [#14223](https://github.com/NixOS/nix/pull/14223) [#14330](https://github.com/NixOS/nix/pull/14330) [#14333](https://github.com/NixOS/nix/pull/14333) [#14335](https://github.com/NixOS/nix/pull/14335) [#14336](https://github.com/NixOS/nix/pull/14336) [#14337](https://github.com/NixOS/nix/pull/14337) [#14350](https://github.com/NixOS/nix/pull/14350) [#14356](https://github.com/NixOS/nix/pull/14356) [#14357](https://github.com/NixOS/nix/pull/14357) [#14374](https://github.com/NixOS/nix/pull/14374) [#14375](https://github.com/NixOS/nix/pull/14375) [#14376](https://github.com/NixOS/nix/pull/14376) [#14377](https://github.com/NixOS/nix/pull/14377) [#14391](https://github.com/NixOS/nix/pull/14391) [#14393](https://github.com/NixOS/nix/pull/14393) [#14420](https://github.com/NixOS/nix/pull/14420) [#14421](https://github.com/NixOS/nix/pull/14421)

  S3 binary cache operations now happen via HTTP, leveraging `libcurl`'s native AWS SigV4 authentication instead of the AWS C++ SDK, providing significant improvements:

  - **Reduced memory usage**: Eliminates memory buffering issues that caused segfaults with large files
  - **Fixed upload reliability**: Resolves AWS SDK chunking errors (`InvalidChunkSizeError`)
  - **Lighter dependencies**: Uses lightweight `aws-crt-cpp` instead of full `aws-cpp-sdk`, reducing build complexity

  The new implementation requires curl >= 7.75.0 and `aws-crt-cpp` for credential management.

  All existing S3 URL formats and parameters remain supported, however the store settings for configuring multipart uploads have changed:

  - **`multipart-upload`** (default: `false`): Enable multipart uploads for large files. When enabled, files exceeding the multipart threshold will be uploaded in multiple parts.

  - **`multipart-threshold`** (default: `100 MiB`): Minimum file size for using multipart uploads. Files smaller than this will use regular PUT requests. Only takes effect when `multipart-upload` is enabled.

  - **`multipart-chunk-size`** (default: `5 MiB`): Size of each part in multipart uploads. Must be at least 5 MiB (AWS S3 requirement). Larger chunk sizes reduce the number of requests but use more memory.

  - **`buffer-size`**: Has been replaced by `multipart-chunk-size` and is now an alias to it.

  Note that this change also means Nix now supports S3 binary cache stores even if built without `aws-crt-cpp`, but only for public buckets which do not require authentication.

- S3 URLs now support object versioning via `versionId` parameter [#13955](https://github.com/NixOS/nix/issues/13955) [#14274](https://github.com/NixOS/nix/pull/14274)

  S3 URLs now support a `versionId` query parameter to fetch specific versions
  of objects from S3 buckets with versioning enabled. This allows pinning to
  exact object versions for reproducibility and protection against unexpected
  changes:

  ```
  s3://bucket/key?region=us-east-1&versionId=abc123def456
  ```

- S3 binary cache stores now support storage class configuration [#7015](https://github.com/NixOS/nix/issues/7015) [#14464](https://github.com/NixOS/nix/pull/14464)

  S3 binary cache stores now support configuring the storage class for uploaded objects via the `storage-class` parameter. This allows users to optimize costs by selecting appropriate storage tiers based on access patterns.

  Example usage:

  ```bash
  # Use Glacier storage for long-term archival
  nix copy --to 's3://my-bucket?storage-class=GLACIER' /nix/store/...

  # Use Intelligent Tiering for automatic cost optimization
  nix copy --to 's3://my-bucket?storage-class=INTELLIGENT_TIERING' /nix/store/...
  ```

  The storage class applies to both regular uploads and multipart uploads. When not specified, objects use the bucket's default storage class.

  See the [S3 storage classes documentation](https://docs.aws.amazon.com/AmazonS3/latest/userguide/storage-class-intro.html) for available storage classes and their characteristics.


## Store path info JSON format changes

The JSON format emitted by `nix path-info --json` has been updated to a new version with improved structure.

To maintain compatibility, `nix path-info --json` now requires a `--json-format` flag to specify the output format version.
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

  The map from store path base names to store object info is nested under the `info` field.

- **Store path base names instead of full paths**:

  Map keys and references use store path base names (e.g., `"abc...-foo"`) instead of full absolute store paths.
  Combined with `storeDir`, the full path can be reconstructed.

- **Structured `ca` field**:

  Content address is now a structured JSON object instead of a string:

  - Old: `"ca": "fixed:r:sha256:1abc..."`
  - New: `"ca": {"method": "nar", "hash": "sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="}`
  - Still `null` values for input-addressed store objects

  The `hash` field uses the [SRI](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) format like other hashes.

Additionally the following fields are added to both formats:

  - **`version` field**:

    All store path info JSON now includes `"version": <1|2>`. The `version` tracks breaking changes, and adding fields to outputted JSON is not a breaking change.

  - **`storeDir` field**:

    Top-level `"storeDir"` field contains the store directory path (e.g., `"/nix/store"`).

## Derivation JSON format changes

The derivation JSON format has been updated from version 3 to version 4:

- **Nested structure with top-level metadata**:

  The output of `nix derivation show` is now wrapped in an object with `version` and `derivations` fields:

  ```json
  {
    "version": 4,
    "derivations": { ... }
  }
  ```

  The map from derivation paths to derivation info is nested under the `derivations` field.

  This matches the structure used for `nix path-info --json --json-format 2`, and likewise brings this command into compliance with the JSON guidelines.

- **Restructured inputs**:

  Inputs are now nested under an `inputs` object:

  - Old: `"inputSrcs": [...], "inputDrvs": {...}`
  - New: `"inputs": {"srcs": [...], "drvs": {...}}`

- **Consistent content addresses**:

  Fixed content-addressed outputs now use structured JSON format.
  This is the same format as `ca` in store path info (after the new version).

Version 3 and earlier formats are *not* accepted when reading.

**Affected command**: `nix derivation`, namely its `show` and `add` sub-commands.

## Miscellaneous changes

- Git fetcher: Restore progress indication [#14487](https://github.com/NixOS/nix/pull/14487)

  Nix used to feel "stuck" while it was cloning large repositories. Nix now shows Git's native progress indicator while fetching.

  Upstreamed from [Determinate Nix 3.13.0](https://github.com/DeterminateSystems/nix-src/pull/250).

- Interrupting REPL commands works more than once [#13481](https://github.com/NixOS/nix/issues/13481)

  Previously, this only worked once per REPL session; further attempts would be ignored.
  This issue is now fixed, so REPL commands such as `:b` or `:p` can be canceled consistently.
  This is a cherry-pick of the change from the [Lix project](https://gerrit.lix.systems/c/lix/+/1097).

- NAR unpacking code has been rewritten to make use of dirfd-based `openat` and `openat2` system calls when available [#14597](https://github.com/NixOS/nix/pull/14597)

- Dynamic size unit rendering [#14423](https://github.com/NixOS/nix/pull/14423) [#14364](https://github.com/NixOS/nix/pull/14364)

  Various commands and the progress bar now use dynamically determined size units instead
  of always using `MiB`. For example, the progress bar now reports download status like:

  ```
  [1/196/197 copied (773.7 MiB/2.1 GiB), 172.4/421.5 MiB DL]
  ```

  Instead of:

  ```
  [1/196/197 copied (773.7/2147.3 MiB), 172.4/421.5 MiB DL]
  ```

## Contributors

This release was made possible by the following 33 contributors:

- Adam Dinwoodie [**(@me-and)**](https://github.com/me-and)
- jonhermansen [**(@jonhermansen)**](https://github.com/jonhermansen)
- Arnout Engelen [**(@raboof)**](https://github.com/raboof)
- Jean-François Roche [**(@jfroche)**](https://github.com/jfroche)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Marcel [**(@MarcelCoding)**](https://github.com/MarcelCoding)
- David McFarland [**(@corngood)**](https://github.com/corngood)
- Soumyadip Sarkar [**(@neuralsorcerer)**](https://github.com/neuralsorcerer)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Tristan Ross [**(@RossComputerGuy)**](https://github.com/RossComputerGuy)
- Alex Auvolat [**(@Alexis211)**](https://github.com/Alexis211)
- edef [**(@edef1c)**](https://github.com/edef1c)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Vinayak Goyal [**(@vinayakankugoyal)**](https://github.com/vinayakankugoyal)
- Graham Dennis [**(@GrahamDennis)**](https://github.com/GrahamDennis)
- Aspen Smith [**(@glittershark)**](https://github.com/glittershark)
- Jens Petersen [**(@juhp)**](https://github.com/juhp)
- Bernardo Meurer [**(@lovesegfault)**](https://github.com/lovesegfault)
- Peter Bynum [**(@pkpbynum)**](https://github.com/pkpbynum)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Alex Decious [**(@adeci)**](https://github.com/adeci)
- Matthieu Coudron [**(@teto)**](https://github.com/teto)
- Domen Kožar [**(@domenkozar)**](https://github.com/domenkozar)
- Taeer Bar-Yam [**(@Radvendii)**](https://github.com/Radvendii)
- Seth Flynn [**(@getchoo)**](https://github.com/getchoo)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Vladimir Panteleev [**(@CyberShadow)**](https://github.com/CyberShadow)
- bryango [**(@bryango)**](https://github.com/bryango)
- Henry [**(@cootshk)**](https://github.com/cootshk)
- Martin Joerg [**(@mjoerg)**](https://github.com/mjoerg)
- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
