# Release 2.35.0 (2026-06-22)

## Highlights

- Sources are copied to the store more lazily [#3121](https://github.com/NixOS/nix/issues/3121) [#15711](https://github.com/NixOS/nix/pull/15711) [#15920](https://github.com/NixOS/nix/pull/15920)

  Historically, flakes source trees have been eagerly fetched to and evaluated from Nix store to ensure deterministic and hermetic evaluation, even if the resulting store object is not used as a derivation input. This made the implementation simpler, yet made flakes unusable in large repositories and performed unnecessary writes to the store on each change to the source tree.

  Since Nix 2.32, all I/O (excluding `path:` and `hg+:`-style inputs) for reading sources during evaluation has been funneled to their original filesystem location (or to the `~/.cache/nix/tarball-cache-v2` bare git repository for tarball-based inputs). However, the source tree was still fetched to the store -- primarily for computing the resulting content-addressed store path. In most cases, (such as importing the `nixpkgs` package set) this is not necessary.

  Touching (and hashing the NAR serialisation of) the whole source tree is unavoidable, since:

  - In case of flake inputs, `narHash` integrity must be checked eagerly.
  - The `outPath` attribute of a flake must be known in advance, and for backwards compatibility must be a content-addressed store path string with [constant string context](@docroot@/language/string-context.md#string-context-constant) representing the flake source tree.

  Even in the limitations imposed by backwards compatibility requirements, there are several improvements that are achievable. To reduce the number of copies performed, Nix now hashes the input without copying first, assuming that the `.outPath` will not end up in a derivation attribute and thus would never have to be actually fetched to the store. This comes at the slight cost or doing more work in case the assumption is wrong, but results in less work in typical use cases. The evaluator continues to behave as if the copy was performed:

  - Flakes are still evaluated from the store, from the evaluator's point of view.
  - `toString ./.` continues to produce a content-addressed store path string without context.
  - Path resolution crossing trees located in the filesystem and in the Nix's view of it (with "virtual" overlays on top) continues to work. For example, the flake source tree can contain a relative symlink pointing outside its corresponding store object (though such usage is discouraged and makes further improvements to laziness intangible).
  - Reading files from the flake's `outPath` continues to work. For example, such code is well-formed and is not considered [IFD](@docroot@/language/import-from-derivation.md):

  ```nix
  builtins.readFile ( /. + (builtins.unsafeDiscardStringContext self.outPath) + "/flake.nix" )
  ```

  Similar treatment has been applied to `builtins.fetchTarball`, which no longer eagerly copies paths to the store.
  `builtins.storePath` now also short-circuits on "lazy-ish" store paths and doesn't substitute unless necessary.

  This change is expected to significantly reduce disk usage required for typical evaluations and results in ~2x speedup for fetching and unpacking a nixpkgs tarball (either via `fetchTree`/flakes or via `fetchTarball`).

- Support FreeBSD `libjail` based sandboxing, add `x86_64-freebsd` to installer [#9968](https://github.com/NixOS/nix/pull/9968) [#13281](https://github.com/NixOS/nix/pull/13281) [#15673](https://github.com/NixOS/nix/pull/15673)

  The FreeBSD build of Nix now supports build sandboxing via FreeBSD jails and is enabled by default.
  A FreeBSD build has been added to the traditional installer script. The beta rust-based installer is not yet supported.
  FreeBSD support is not as well-tested as Linux or macOS, but is fully capable of building packages and performing other tasks expected of Nix on Linux.

## Improvements

- HTTP/3 (QUIC) support

  Nix can now fetch from binary caches and other HTTP(S) sources over HTTP/3 (QUIC), controlled by a new [`http3`](@docroot@/command-ref/conf-file.md#conf-http3) setting (disabled by default).
  When enabled, Nix requests HTTP/3 and transparently falls back to HTTP/2 or HTTP/1.1 for servers that do not advertise QUIC.
  The setting only takes effect when linked against a `libcurl` built with HTTP/3 support, otherwise it is ignored and Nix keeps using HTTP/2 without warning or error.

  Enable it with:

  ```
  nix.conf: http3 = true
  CLI:      --http3
  ```

  Or disable with:

  ```
  nix.conf: http3 = false
  CLI:      --no-http3
  ```

- Link mimalloc for faster evaluation [#15596](https://github.com/NixOS/nix/pull/15596)

  The `nix` binary now links [mimalloc](https://github.com/microsoft/mimalloc) by default, replacing glibc's malloc for all non-GC allocations.
  This yields a **5–12% wall-clock improvement** on evaluation workloads, ranging from `nix-instantiate hello` to `nix-env -qa` and full NixOS configurations.
  The allocator can be disabled at build time with `-Dmimalloc=disabled`.

- The `revCount` attribute of the Git fetchers is now lazily computed and passed-through as-is when explicitly specified [#15772](https://github.com/NixOS/nix/pull/15772) [#14596](https://github.com/NixOS/nix/pull/14596)

  `revCount` and `lastModified` attributes passed to the Git fetcher are no longer eagerly validated when explicitly specified.

  When not explicitly specified, `revCount` is now also a thunk value and not computed eagerly. This delays this (potentially) expensive computation until the value is actually required.

- Configurable file-transfer retry backoff with full jitter and `Retry-After` support [#15023](https://github.com/NixOS/nix/issues/15023) [#15419](https://github.com/NixOS/nix/issues/15419) [#15449](https://github.com/NixOS/nix/pull/15449)

  File transfer retries (downloads and uploads) now use AWS-style "full jitter" exponential backoff, treat HTTP 503 as rate-limited (same longer delay as 429),
  and honor the `Retry-After` response header.

  Retry timing is configurable via new `nix.conf` settings:

  - [`filetransfer-retry-delay`](@docroot@/command-ref/conf-file.md#conf-filetransfer-retry-delay): base delay for transient errors
  - [`filetransfer-retry-delay-rate-limited`](@docroot@/command-ref/conf-file.md#conf-filetransfer-retry-delay-rate-limited): base delay for 429/503
  - [`filetransfer-retry-max-delay`](@docroot@/command-ref/conf-file.md#conf-filetransfer-retry-max-delay): per-attempt delay ceiling
  - [`filetransfer-retry-jitter`](@docroot@/command-ref/conf-file.md#conf-filetransfer-retry-jitter): enable full jitter

  The existing `download-attempts` setting has been renamed to [`filetransfer-retry-attempts`](@docroot@/command-ref/conf-file.md#conf-filetransfer-retry-attempts) to reflect that it applies to uploads as well as downloads.
  The old name remains as an alias for backwards compatibility.

  Per-substituter overrides are available as store reference parameters ([`retry-delay`](@docroot@/store/types/http-binary-cache-store.md#store-http-binary-cache-store-retry-delay), [`retry-delay-rate-limited`](@docroot@/store/types/http-binary-cache-store.md#store-http-binary-cache-store-retry-delay-rate-limited), [`retry-max-delay`](@docroot@/store/types/http-binary-cache-store.md#store-http-binary-cache-store-retry-max-delay), [`retry-attempts`](@docroot@/store/types/http-binary-cache-store.md#store-http-binary-cache-store-retry-attempts)), e.g. `s3://my-cache?retry-attempts=8`.

- Improve daemon socket path logic for chroot stores [#15190](https://github.com/NixOS/nix/pull/15190)

  The default daemon socket path now uses the per-store [`state`](@docroot@/store/types/local-store.md#store-local-store-state) directory whenever one is defined, rather than always using the global [`NIX_STATE_DIR`](@docroot@/command-ref/env-common.md#env-NIX_STATE_DIR).
  This means [local chroot stores](@docroot@/store/types/local-store.md#chroot) each get their own socket path automatically.

  Example:

  ```bash
  nix-daemon --store /foo/bar
  ```

  will now use a socket at:
  ```
  /foo/bar/nix/var/nix/daemon-socket/socket
  ```
  instead of
  ```
  $NIX_STATE_DIR/daemon-socket/socket
  ```

  Users who wish to serve or connect to a chroot store at the old location will have to force the socket location:

  - When serving (running a daemon), use the new [`--socket-path`](@docroot@/command-ref/new-cli/nix3-daemon.md#opt-socket-path) flag:

    ```bash
    nix daemon --socket-path "$NIX_STATE_DIR/daemon-socket/socket"
    ```

  - When connecting as a client  put the path in the [store URL](@docroot@/store/types/local-daemon-store.md):

    ```
    unix://$NIX_STATE_DIR/daemon-socket/socket
    ```

- Linux sandbox: also block `listxattr` syscalls [#15743](https://github.com/NixOS/nix/pull/15743)

  The Linux sandbox now also returns `ENOTSUP` for `listxattr`, `llistxattr` and `flistxattr`, matching the existing treatment of `getxattr`/`setxattr`/`removexattr`.
  This prevents host xattrs (e.g. `security.selinux`) from leaking into builds and fixes tools such as `mkfs.ubifs` that probe xattr support via `listxattr`.

- Support SCP-like URLs in fetchGit and type = "git" flake inputs [#14852](https://github.com/NixOS/nix/issues/14852) [#14867](https://github.com/NixOS/nix/issues/14867) [#14863](https://github.com/NixOS/nix/pull/14863)

  Nix now (once again) recognizes [SCP-like syntax for Git URLs](https://git-scm.com/docs/git-clone#_git_urls). This partially
  restores compatibility with Nix 2.3 for `fetchGit`. The following syntax is once again supported:

  ```nix
  builtins.fetchGit "host:/absolute/path/to/repo"
  ```

  Nix also passes through the tilde (for home directories) verbatim:

  ```nix
  builtins.fetchGit "host:~/relative/to/home"
  ```

  IPv6 addresses also supported when bracketed:

  ```nix
  builtins.fetchGit "user@[::1]:~/relative/to/home"
  ```

  `builtins.fetchTree` also supports this syntax now:

  ```nix
  builtins.fetchTree { type = "git"; url = "host:/path/to/repo"; }
  ```

- `nix flake check` now supports `--print-out-paths` [#13470](https://github.com/NixOS/nix/issues/13470) [#15476](https://github.com/NixOS/nix/pull/15476) and `--out-link` [#13470](https://github.com/NixOS/nix/issues/13470) [#15476](https://github.com/NixOS/nix/pull/15476) defaulting to not creating out links if the flag is not specified.

- Added `--skip-alive` option to `nix store delete` for collecting garbage within a closure [#7239](https://github.com/NixOS/nix/issues/7239) [#15236](https://github.com/NixOS/nix/pull/15236) [#15727](https://github.com/NixOS/nix/pull/15727)

  `nix store delete --recursive --skip-alive` can be used to collect garbage within a closure, in which case it will only collect the dead paths that are part of the closure of its arguments.
  The additional option `--also-referrers` is added to support this mode, which allows referrers of paths in the closure to also be deleted.

- `builtins.getFlake` now supports path values [#15290](https://github.com/NixOS/nix/pull/15290)

  `builtins.getFlake` now accepts path values in addition to flakerefs. This improves the usability of relative flakes, allowing you to write `builtins.getFlake ./subflake`.
  This change does not allow specifying paths that are not already in the store (though they do not have valid store objects, i.e. this will not force a copy if the flake has only been hashed -- and not copied to the store). This may change in a future release.

- `nix-profile.fish` and `nix-profile-daemon.fish` now use `$NIX_LINK` for computing the value of `NIX_PROFILE` instead of `$HOME/.nix-profile` [#14293](https://github.com/NixOS/nix/pull/14293)

- The computed Git LFS endpoint URLs have been fixed to follow the spec [#15891](https://github.com/NixOS/nix/pull/15891) and memory usage of LFS fetches has been decreased [#15912](https://github.com/NixOS/nix/pull/15912).

- Primop documentation now includes time complexity information [#14554](https://github.com/NixOS/nix/pull/14554).

- Improved documentation on store paths and derivation building [#14699](https://github.com/NixOS/nix/pull/14699).

- The build hook is now killed with `SIGTERM` instead of `SIGKILL` [#15105](https://github.com/NixOS/nix/pull/15105).

## Backwards incompatible changes

- Content-addressed derivations: realisations keyed by store path instead of hash modulo [#11897](https://github.com/NixOS/nix/issues/11897) [#12464](https://github.com/NixOS/nix/pull/12464)

  The experimental content-addressed (CA) derivation feature has undergone a significant change to how build traces (formerly called "realisations") are identified. This affects the **binary cache protocol** and the **wire protocols**.

  ### What changed

  #### Build trace format

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

  #### Build trace usage

  Previously the build trace contained entries for both unresolved and [resolved](@docroot@/store/resolution.md) derivations.
  Now, it only contains entries for resolved derivations.
  For now, unresolved derivations will be resolved from these underlying build trace entries.
  This is slower, but avoids a bunch of correctness issues.

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
        "signatures": [{ "keyName": "cache.example.com-1", "sig": "..." }]
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

  ### Local Store SQLite schema

  The build trace entries no longer have any foreign key store objects in the store.
  This is because we will need to remember the build trace entries for resolved derivations we may have deleted, otherwise we will effectively forget outputs resolved derivations we do have on disk.
  GC for build trace will be implemented later --- there is no single correct choice (there is no closure property) so it will be a question of what policies users want.

  ### Structured signatures

  [Signatures](@docroot@/protocols/json/signature.md) in JSON formats are now represented as structured objects with `keyName` and `sig` fields, rather than colon-separated strings.
  `nix path-info --json --json-format 3` opts into the new version for this command.
  JSON parsing accepts both the old string format and new structured format for backwards compatibility.

  ### Impact

  - **Non-CA derivation users**: No impact. This only affects the experimental `ca-derivations` feature.
  - **Binary cache operators**:
    Binary caches serving CA derivation build traces will need to be repopulated.
    Existing NARs and narinfo files are unaffected.
  - **Tool authors**:
    Implementations interfacing with the CA derivations protocol are simplified.
    The derivation hash modulo algorithm is no longer required to form build trace keys.

## Build performance improvements

- Make post-build-hook asynchronous [#15406](https://github.com/NixOS/nix/issues/15406) [#15451](https://github.com/NixOS/nix/pull/15451)

  The [`post-build-hook`](@docroot@/command-ref/conf-file.md#conf-post-build-hook) now runs asynchronously, without blocking the build event loop.
  Dependent builds are not started until the hook finishes, but multiple hook instances are now launched concurrently -- up to the [`max-jobs`](@docroot@/command-ref/conf-file.md#conf-max-jobs) limit.

- zstd compression now emits multi-frame output and uses less memory [#15550](https://github.com/NixOS/nix/pull/15550)

  zstd-compressed NARs are now written as a sequence of independent 16 MiB frames instead of a single large frame.
  This lays the groundwork for parallel decompression in a future release without requiring caches to be repopulated, and significantly lowers peak memory use during compression
  (e.g. from ~600 MiB to ~100 MiB for a 1 GiB store path).

  The output remains standard zstd and is decoded unchanged by existing Nix binaries and the `zstd` CLI; compression ratio is effectively unchanged.

  Per-frame compression now uses up to 4 worker threads. For zstd this is the new default: the [`parallel-compression`](@docroot@/store/types/http-binary-cache-store.md#store-http-binary-cache-store-parallel-compression) store setting defaults to `true` when `compression=zstd` (it remains `false` for other compression algorithms like `xz`).
  Set `?parallel-compression=false` to opt out.

- The derivation build scheduler memory usage reduction and performance improvements [#15611](https://github.com/NixOS/nix/pull/15611) [#15695](https://github.com/NixOS/nix/pull/15695)

  Memory usage of the derivation build scheduler has been improved to allow more state sharing.
  Inefficiencies leading to quadratic complexity of scheduling build/substitution jobs have been addressed.
  Scheduling resources are allocated more sparingly and freed earlier to reduce peak consumption.

  These improvements amount to ~2-8x less `nix-daemon` memory usage for typical workloads and more in larger derivation graphs, not accounting for short-lived allocations used during substitution.

  Notably, the current architecture of the build scheduler gets proportionally slower on Linux with larger heaps as derivation "builder" processes are `fork`-ed directly from the Nix process, which blocks the builder event loop for the duration of the `fork`. Thus, smaller heap of `nix-daemon` translates into faster build startups.

## Bug fixes

- Fix hash collision between store paths with self-references and their zeroed-out equivalents [#15837](https://github.com/NixOS/nix/issues/15837) [#15931](https://github.com/NixOS/nix/pull/15931)

  When computing the hash of a NAR with self-references, Nix zeroes out the self-references but also hashes their positions.
  The latter was accidentally lost in Nix 2.17.0, which meant a NAR with self-references could hash to the same store path as an otherwise-identical NAR in which some of the self-references had been zeroed out.

  This release restores hashing the positions of self-references.
  As a consequence, content-addressed store paths derived from self-referential NARs will differ from those produced by Nix 2.17 through 2.34.
  This affects users of the experimental `ca-derivations` features, as well as users of `nix store make-content-addressed`.

- C API: Fix `EvalState` pointer passed to primop callbacks [#15300](https://github.com/NixOS/nix/pull/15300) [#15383](https://github.com/NixOS/nix/pull/15383)

  The `EvalState *` passed to C API primop callbacks was incorrectly pointing to the internal `nix::EvalState` rather than the C API wrapper struct.
  This caused a segfault when the callback used the pointer with C API functions such as `nix_alloc_value()`.
  The same issue affected `printValueAsJSON` and `printValueAsXML` callbacks on external values.

- GitHub fetcher now validates URL parameters [#15304](https://github.com/NixOS/nix/issues/15304) [#15331](https://github.com/NixOS/nix/pull/15331)

  The `github:` fetcher now validates URL parameters, and will error if an invalid parameter like `tag` is provided.

- Fixed a bug where keep-outputs and keep-derivations can interfere with delete commands [#15776](https://github.com/NixOS/nix/pull/15776)

  Setting [`keep-derivations`](@docroot@/command-ref/conf-file.md#conf-keep-derivations) to `true` and trying to delete a derivation with realised outputs would previously fail.
  Same with [`keep-outputs`](@docroot@/command-ref/conf-file.md#conf-keep-outputs) and trying to delete an output that still has derivers.
  These options no longer affect the deletion commands, and are now documented as such.

- S3 substituters fall back to the URL's region for STS WebIdentity auth [#15594](https://github.com/NixOS/nix/pull/15594)

  When authenticating to an S3 binary cache via STS WebIdentity (EKS IRSA, GitHub Actions OIDC), Nix now uses the `?region=` parameter from the S3 URL as a fallback for the STS endpoint region if neither `AWS_REGION` nor `AWS_DEFAULT_REGION` is set.
  Previously, IRSA setups that exported `AWS_WEB_IDENTITY_TOKEN_FILE` and `AWS_ROLE_ARN` but no region would fail with a misleading "IMDS provider" error.

- S3: restore STS WebIdentity and ECS container credential providers [#15507](https://github.com/NixOS/nix/pull/15507)

  Nix 2.33 replaced the S3 backend's `aws-sdk-cpp` credential chain with a custom chain built on `aws-c-auth`.
  That chain omitted two providers, breaking S3 binary cache access in container workloads:

  - **STS WebIdentity** (`AWS_WEB_IDENTITY_TOKEN_FILE`, `AWS_ROLE_ARN`, `AWS_ROLE_SESSION_NAME`) -- used by EKS IRSA, GitHub Actions OIDC, and any `sts:AssumeRoleWithWebIdentity` federation.
  - **ECS container metadata** (`AWS_CONTAINER_CREDENTIALS_RELATIVE_URI`, `AWS_CONTAINER_CREDENTIALS_FULL_URI`) -- used by ECS tasks and EKS Pod Identity.

  The typical symptom was a misleading IMDS error (`Valid credentials could not be sourced by the IMDS provider`), because IMDS is the last provider tried after the correct one was skipped.

  Both providers are now part of the chain, ordered to match the pre-2.33 behaviour.
  As in both the old and new AWS SDK default chains, ECS and IMDS are mutually exclusive: when container credential environment variables are set, IMDS is skipped.

- Fixed build failures on systems with the `unprivileged_userns_clone=0` kernel option [#15131](https://github.com/NixOS/nix/pull/15131).

## Contributors

This release was made possible by the following 58 contributors:

- Michael Wang [**(@zwang20)**](https://github.com/zwang20)
- Amaan Qureshi [**(@amaanq)**](https://github.com/amaanq)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Reuben Gardos Reid [**(@ReubenJ)**](https://github.com/ReubenJ)
- StepBroBD [**(@stepbrobd)**](https://github.com/stepbrobd)
- dram [**(@dramforever)**](https://github.com/dramforever)
- Tom [**(@thunze)**](https://github.com/thunze)
- Sergei Trofimovich [**(@trofi)**](https://github.com/trofi)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- steveoliphant [**(@steveoliphant)**](https://github.com/steveoliphant)
- espes [**(@espes)**](https://github.com/espes)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Artemis Tosini [**(@artemist)**](https://github.com/artemist)
- sander [**(@sandydoo)**](https://github.com/sandydoo)
- Erik Jensen [**(@rkjnsn)**](https://github.com/rkjnsn)
- Cameron Will [**(@cwill747)**](https://github.com/cwill747)
- Maciej Krüger [**(@mkg20001)**](https://github.com/mkg20001)
- Dror Speiser [**(@drorspei)**](https://github.com/drorspei)
- Eveeifyeve [**(@Eveeifyeve)**](https://github.com/Eveeifyeve)
- Audrey Dutcher [**(@rhelmot)**](https://github.com/rhelmot)
- Lisanna Dettwyler [**(@lisanna-dettwyler)**](https://github.com/lisanna-dettwyler)
- TyIsI [**(@TyIsI)**](https://github.com/TyIsI)
- Adam Kliś [**(@BonusPlay)**](https://github.com/BonusPlay)
- Domen Kožar [**(@domenkozar)**](https://github.com/domenkozar)
- Taeer Bar-Yam [**(@Radvendii)**](https://github.com/Radvendii)
- ryota2357 [**(@ryota2357)**](https://github.com/ryota2357)
- LIN, Jian [**(@jian-lin)**](https://github.com/jian-lin)
- znmz [**(@znmz)**](https://github.com/znmz)
- Felix Stupp [**(@Zocker1999NET)**](https://github.com/Zocker1999NET)
- Johannes Kirschbauer [**(@hsjobeki)**](https://github.com/hsjobeki)
- Antonio Nuno Monteiro [**(@anmonteiro)**](https://github.com/anmonteiro)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- adisbladis [**(@adisbladis)**](https://github.com/adisbladis)
- Luna Nova [**(@LunNova)**](https://github.com/LunNova)
- Riccardo Mazzarini [**(@noib3)**](https://github.com/noib3)
- Bouke van der Bijl [**(@bouk)**](https://github.com/bouk)
- Dario [**(@dve00)**](https://github.com/dve00)
- Michael Hoang [**(@Enzime)**](https://github.com/Enzime)
- Paul Sbarra [**(@tones111)**](https://github.com/tones111)
- edef [**(@edef1c)**](https://github.com/edef1c)
- Adam Dinwoodie [**(@me-and)**](https://github.com/me-and)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- Jeremy Fleischman [**(@jfly)**](https://github.com/jfly)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Alex Ionescu [**(@aionescu)**](https://github.com/aionescu)
- Tristan Ross [**(@RossComputerGuy)**](https://github.com/RossComputerGuy)
- Bernardo Meurer [**(@lovesegfault)**](https://github.com/lovesegfault)
- Pierre Penninckx [**(@ibizaman)**](https://github.com/ibizaman)
- Leonard Sheng Sheng Lee [**(@sheeeng)**](https://github.com/sheeeng)
- rszyma [**(@rszyma)**](https://github.com/rszyma)
- Ryan Hendrickson [**(@rhendric)**](https://github.com/rhendric)
- Lennart Kolmodin [**(@kolmodin)**](https://github.com/kolmodin)
- zowoq [**(@zowoq)**](https://github.com/zowoq)
- Peter Collingbourne [**(@pcc)**](https://github.com/pcc)
- Simon Žlender [**(@szlend)**](https://github.com/szlend)
- Lily Foster [**(@lilyinstarlight)**](https://github.com/lilyinstarlight)
- randomizedcoder [**(@randomizedcoder)**](https://github.com/randomizedcoder)
- Krish Jaiswal
