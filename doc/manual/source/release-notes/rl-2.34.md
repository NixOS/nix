# Release 2.34.0 (2026-02-27)

## Highlights

- Rust nix-installer in beta

  The Rust-based rewrite of the Nix installer is now in beta.
  We'd love help testing it out!

  To test out the new installer, run:
  ```
  curl -sSfL https://artifacts.nixos.org/nix-installer | sh -s -- install
  ```

  This installer can be run even when you have an existing, script-based Nix installation without any adjustments.

  This new installer also comes with the ability to uninstall your Nix installation; run:
  ```
  /nix/nix-installer uninstall
  ```

  This will get rid of your entire Nix installation (even if you installed over an existing, script-based installation).

  This installer is a modified version of the [Determinate Nix Installer](https://github.com/DeterminateSystems/nix-installer) by Determinate Systems.
  Thanks to Determinate Systems for all the investment they've put into the installer.

  Source for the installer is in <https://github.com/NixOS/nix-installer>.
  Report any issues in that repo.

  For CI usage, a GitHub Action to install Nix using this installer is available at <https://github.com/NixOS/nix-installer-action>.

- Stabilisation of `no-url-literals` experimental feature and new diagnostics infrastructure, with `lint-url-literals`, `lint-short-path-literals`, and `lint-absolute-path-literals` settings [#8738](https://github.com/NixOS/nix/issues/8738) [#10048](https://github.com/NixOS/nix/issues/10048) [#10281](https://github.com/NixOS/nix/issues/10281) [#15326](https://github.com/NixOS/nix/pull/15326)

  Experimental feature `no-url-literals` has been stabilised and is now controlled by the `lint-url-literals` option.
  New diagnostics infrastructure has been added for linting discouraged language features.

  ### New lint infrastructure

  #### [`lint-url-literals`](@docroot@/command-ref/conf-file.md#conf-lint-url-literals)

  The `no-url-literals` experimental feature has been stabilised and replaced with a new [`lint-url-literals`](@docroot@/command-ref/conf-file.md#conf-lint-url-literals) setting.

  To migrate from the experimental feature, replace:
  ```
  experimental-features = no-url-literals
  ```
  with:
  ```
  lint-url-literals = fatal
  ```

  #### [`lint-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-short-path-literals)

  The [`warn-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-warn-short-path-literals) boolean setting has been deprecated and replaced with [`lint-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-short-path-literals).

  To migrate, replace:
  ```
  warn-short-path-literals = true
  ```
  with:
  ```
  lint-short-path-literals = warn
  ```

  #### [`lint-absolute-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-absolute-path-literals)

  A new [`lint-absolute-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-absolute-path-literals) setting has been added to control handling of absolute path literals (paths starting with `/`) and home path literals (paths starting with `~/`).

  #### Setting values

  All three settings accept three values:
  - `ignore`: Allow the feature without emitting any diagnostic (default)
  - `warn`: Emit a warning when the feature is used
  - `fatal`: Treat the feature as a parse error

  The defaults may change in future versions.

- Improved parser error messages [#15092](https://github.com/NixOS/nix/pull/15092)

  Parser error messages now use legible strings for tokens instead of internal names. For example, malformed expression `a ++ ++ b` now produces the following error:
  ```
  error: syntax error, unexpected '++'
       at «string»:1:6:
            1| a ++ ++ b
             |      ^
  ```

  Instead of:
  ```
  error: syntax error, unexpected CONCAT
       at «string»:1:6:
            1| a ++ ++ b
             |      ^
  ```

## New features

- `nix repl` now supports `inherit` and multiple bindings [#15082](https://github.com/NixOS/nix/pull/15082)

  The `nix repl` now supports `inherit` statements and multiple bindings per line:

  ```
  nix-repl> a = { x = 1; y = 2; }
  nix-repl> inherit (a) x y
  nix-repl> x + y
  3

  nix-repl> p = 1; q = 2;
  nix-repl> p + q
  3

  nix-repl> foo.bar.baz = 1;
  nix-repl> foo.bar
  { baz = 1; }
  ```

- New command `nix store roots-daemon` for serving GC roots [#15143](https://github.com/NixOS/nix/pull/15143)

  New command [`nix store roots-daemon`](@docroot@/command-ref/new-cli/nix3-store-roots-daemon.md) runs a daemon that serves garbage collector roots over a Unix domain socket.
  It enables the garbage collector to discover runtime roots when the main Nix daemon doesn't have `CAP_SYS_PTRACE` capability and therefore cannot scan `/proc`.

  The garbage collector can be configured to use this daemon via the [`use-roots-daemon`](@docroot@/store/types/local-store.md#store-experimental-option-use-roots-daemon) store setting.

  This feature requires the [`local-overlay-store` experimental feature](@docroot@/development/experimental-features.md#xp-feature-local-overlay-store).

- New command `nix-nswrapper` in `libexec` [#15183](https://github.com/NixOS/nix/pull/15183)

  The new command `libexec/nix-nswrapper` is used to run the Nix daemon in an unprivileged user namespace on Linux. In order to use this command, build user UIDs and GIDs must be allocated in `/etc/subuid` and `/etc/subgid`.

  It can be used to run the Nix daemon with full sandboxing without executing as root. Support has been added to Nixpkgs with the new `nix.daemonUser` and `nix.daemonGroup` settings.

- New setting `ignore-gc-delete-failure` for local stores [#15054](https://github.com/NixOS/nix/pull/15054)

  A new local store setting [`ignore-gc-delete-failure`](@docroot@/store/types/local-store.md#store-local-store-ignore-gc-delete-failure) has been added.
  When enabled, garbage collection will log warnings instead of failing when it cannot delete store paths.
  This is useful when running Nix as an unprivileged user that may not have write access to all paths in the store.

  This setting is experimental and requires the [`local-overlay-store`](@docroot@/development/experimental-features.md#xp-feature-local-overlay-store) experimental feature.

- New setting `narinfo-cache-meta-ttl` [#15287](https://github.com/NixOS/nix/pull/15287)

  The new setting `narinfo-cache-meta-ttl` controls how long binary cache metadata (i.e. `/nix-cache-info`) is cached locally, in seconds. This was previously hard-coded to 7 days, which is still the default. As a result, you can now use `nix store info --refresh` to check whether a binary cache is still valid.

- Support HTTPS binary caches using mTLS (client certificate) authentication [#13002](https://github.com/NixOS/nix/issues/13002) [#13030](https://github.com/NixOS/nix/pull/13030)

  Added support for `tls-certificate` and `tls-private-key` options in substituter URLs.

  Example:

  ```
  https://substituter.invalid?tls-certificate=/path/to/cert.pem&tls-private-key=/path/to/key.pem
  ```

  When these options are configured, Nix will use this certificate/private key pair to authenticate to the server.

- `nix store gc --dry-run` and `nix-collect-garbage --dry-run` now report the number of paths that would be freed [#15229](https://github.com/NixOS/nix/pull/15229) [#5704](https://github.com/NixOS/nix/issues/5704)

## Performance improvements

- Unpacking tarballs to `~/.cache/nix/tarball-cache-v2` is now multithreaded [#12087](https://github.com/NixOS/nix/pull/12087)

  Content-addressed cache for `builtins.fetchTarball` and tarball-based flake inputs (e.g. `github:NixOS/nixpkgs`, `https://channels.nixos.org/nixos-25.11/nixexprs.tar.xz`) now writes git blobs (files) to the `tarball-cache-v2` repository concurrently, which significantly reduces the wall time for tarball unpacking (up to ~1.8x faster unpacking for `https://channels.nixos.org/nixos-25.11/nixexprs.tar.xz` in our testing).

  Currently, Nix doesn't perform any maintenance on the `~/.cache/nix/tarball-cache-v2` repository, which will be addressed in future versions. Users that wish to reclaim disk space used by the tarball cache may want to run:

  ```
  rm -rf ~/.cache/nix/tarball-cache # Historical tarball-cache, not used by Nix >= 2.33
  cd ~/.cache/nix/tarball-cache-v2 && git multi-pack-index write && git multi-pack-index repack && git multi-pack-index expire
  ```

- `nix nar ls` and other NAR listing operations have been optimised further [#15163](https://github.com/NixOS/nix/pull/15163)

- Evaluator hot-path optimizations [#15270](https://github.com/NixOS/nix/pull/15270) [#15271](https://github.com/NixOS/nix/pull/15271)

## C API Changes

- New store API methods [#14766](https://github.com/NixOS/nix/pull/14766) [#14768](https://github.com/NixOS/nix/pull/14768)

  The C API now includes additional methods:

  - `nix_store_query_path_from_hash_part()` - Get the full store path given its hash part
  - `nix_store_copy_path()` - Copy a single store path between two stores, allows repairs and configuring signature checking

- Errors returned from your primops are not treated as recoverable by default [#13930](https://github.com/NixOS/nix/pull/13930) [#15286](https://github.com/NixOS/nix/pull/15286)

  Nix 2.34 by default remembers the error in the thunk that triggered it.

  Previously the following sequence of events worked:

  1. Have a thunk that invokes a primop that's defined through the C API
  2. The primop returns an error
  3. Force the thunk again
  4. The primop returns a value
  5. The thunk evaluated successfully

  **Resolution**

  C API consumers that rely on this must change their recoverable error calls:

  ```diff
  -nix_set_err_msg(context, NIX_ERR_*, msg);
  +nix_set_err_msg(context, NIX_ERR_RECOVERABLE, msg);
  ```

## Bug fixes

- Avoid dropping ssh connections with `ssh-ng://` stores for store path copying [#14998](https://github.com/NixOS/nix/pull/14998) [#6950](https://github.com/NixOS/nix/issues/6950)

  Due to a bug in how Nix handled Boost.Coroutine2 suspension and resumption, copying from `ssh-ng://` stores would drop the SSH connection for each copied path. This issue has been fixed, which improves performance by avoiding multiple SSH/Nix Worker Protocol handshakes.

- S3 binary caches now use virtual-hosted-style addressing by default [#15208](https://github.com/NixOS/nix/issues/15208) [#15216](https://github.com/NixOS/nix/pull/15216)

  S3 binary caches now use virtual-hosted-style URLs
  (`https://bucket.s3.region.amazonaws.com/key`) instead of path-style URLs
  (`https://s3.region.amazonaws.com/bucket/key`) when connecting to standard AWS
  S3 endpoints. This enables HTTP/2 multiplexing and fixes TCP connection
  exhaustion (TIME_WAIT socket accumulation) under high-concurrency workloads.

  A new `addressing-style` store option controls this behavior:

  - `auto` (default): virtual-hosted-style for standard AWS endpoints, path-style
    for custom endpoints.
  - `path`: forces path-style addressing (deprecated by AWS).
  - `virtual`: forces virtual-hosted-style addressing (bucket names must not
    contain dots).

  Bucket names containing dots (e.g., `my.bucket.name`) automatically fall back
  to path-style addressing in `auto` mode, because dotted names create
  multi-level subdomains that break TLS wildcard certificate validation.

  Example using path-style for backwards compatibility:

  ```
  s3://my-bucket/key?region=us-east-1&addressing-style=path
  ```

  Additionally, TCP keep-alive is now enabled on all HTTP connections, preventing
  idle connections from being silently dropped by intermediate network devices
  (NATs, firewalls, load balancers).

- `nix-prefetch-url --unpack` now properly checks for empty archives [#15242](https://github.com/NixOS/nix/pull/15242)

  Prior versions failed to check for empty archives and would crash with a `nullptr` dereference when unpacking empty archives.
  This is now fixed.

- Prevent runaway processes when Nix is killed with `SIGKILL` when building in a local store with build users [#15193](https://github.com/NixOS/nix/pull/15193)

  When run as root, Nix doesn't run builds via the daemon and is a parent of the forked build processes. Prior versions of Nix failed to preserve the `PR_SET_PDEATHSIG` parent-death signal across `setuid` calls. This could lead to build processes being reparented and continue running in the background. This has been fixed.

- Fix crash when interrupting `--log-format internal-json` [#15335](https://github.com/NixOS/nix/pull/15335)

  Pressing Ctrl-C during `--log-format internal-json` (used by [nix-output-monitor](https://github.com/maralorn/nix-output-monitor)) no longer causes a spurious "Nix crashed. This is a bug." report.

- Fix percent-encoding in `file://` and `local://` store URIs [#15280](https://github.com/NixOS/nix/pull/15280)

  Store URIs with special characters like `+` in the path (e.g. `file:///tmp/a+b`) no longer incorrectly create percent-encoded directories (e.g. `/tmp/a%2Bb`).

- Fix crash during tab completion in `nix repl` [#15255](https://github.com/NixOS/nix/pull/15255)

- Fix "Too many open files" on macOS [#15205](https://github.com/NixOS/nix/pull/15205)

  Nix now raises the open file soft limit to the hard limit at startup, fixing "Too many open files" errors on macOS where the default soft limit is low.

- `nix develop` no longer fails when `inputs.nixpkgs` has `flake = false` [#15175](https://github.com/NixOS/nix/pull/15175)

- `builtins.flakeRefToString` no longer fails with "attribute is a thunk" [#15160](https://github.com/NixOS/nix/pull/15160)

- Fix `QueryPathInfo` throwing on invalid paths in the daemon [#15134](https://github.com/NixOS/nix/pull/15134)

- `nix-store --generate-binary-cache-key` now fsyncs key files to prevent corruption [#15107](https://github.com/NixOS/nix/pull/15107)

- Fix `build-hook` setting in `nix.conf` being ignored [#15083](https://github.com/NixOS/nix/pull/15083)

- Fix empty error messages when builds are cancelled due to a dependency failure [#14972](https://github.com/NixOS/nix/pull/14972)

  When a build fails without `--keep-going`, other in-progress builds are cancelled. Previously, these cancelled builds were incorrectly reported as failed with empty error messages. This affected `buildPathsWithResults` callers such as `nix flake check`.

## Miscellaneous changes

- Content-Encoding decompression is now handled by libcurl [#14324](https://github.com/NixOS/nix/issues/14324) [#15336](https://github.com/NixOS/nix/pull/15336)

  Transparent decompression of HTTP downloads specifying `Content-Encoding` header now uses libcurl. This adds support for previously advertised, but not supported `deflate` encoding as well as deprecated `x-gzip` alias.
  Non-standard `xz`, `bzip2` encodings that were previously advertised are no longer supported, as they do not commonly appear in the wild and should not be sent by compliant servers.

  `br`, `zstd`, `gzip` continue to be supported. Distro packaging should ensure that the `libcurl` dependency is linked against required libraries to support these encodings. By default, the build system now requires libcurl >= 8.17.0, which is not known to have issues around [pausing and decompression](https://github.com/curl/curl/issues/16280).

- Static builds now support S3 features (`libstore:s3-aws-auth` meson option) [#15076](https://github.com/NixOS/nix/pull/15076)

- Improved package-related error messages [#15349](https://github.com/NixOS/nix/pull/15349)

  Store path context is now rendered in the user-facing `hash^out` format instead of the internal `!out!hash` format.
  A misleading error message in `nix-env` that incorrectly blamed content-addressed derivations has been fixed.

- Improved error message for empty derivation files [#15298](https://github.com/NixOS/nix/pull/15298)

  Parsing an empty `.drv` file (e.g. due to store corruption after an unclean shutdown) now produces a clear error message instead of the cryptic `expected string 'D'`.

- Relative `file:` paths for tarballs are now rejected with a clear error [#14983](https://github.com/NixOS/nix/pull/14983)

- Continued progress on the Windows port, including build fixes, CI improvements, and platform abstractions.

- Nix docker images are now uploaded to [GHCR](https://github.com/NixOS/nix/pkgs/container/nix) as part of the release process

  Historically, only pre-release builds of `amd64` docker images have been uploaded to ghcr.io with the `latest` tag pointing to the last built image from `master` branch. This has been fixed and going forward, <https://github.com/NixOS/nix/pkgs/container/nix> will include the same images as <https://hub.docker.com/r/nixos/nix/> that are built by [Hydra](https://hydra.nixos.org/project/nix) for [arm64](https://hydra.nixos.org/job/nix/maintenance-2.34/dockerImage.aarch64-linux) and [amd64](https://hydra.nixos.org/job/nix/maintenance-2.34/dockerImage.x86_64-linux). Pre-release versions are no longer pushed to the registry.

## Contributors

This release was made possible by the following 43 contributors:

- Taeer Bar-Yam [**(@Radvendii)**](https://github.com/Radvendii)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Graham Dennis [**(@GrahamDennis)**](https://github.com/GrahamDennis)
- Damien Diederen [**(@ztzg)**](https://github.com/ztzg)
- koberbe-jh [**(@koberbe-jh)**](https://github.com/koberbe-jh)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Bouke van der Bijl [**(@bouk)**](https://github.com/bouk)
- Lisanna Dettwyler [**(@lisanna-dettwyler)**](https://github.com/lisanna-dettwyler)
- kiara [**(@KiaraGrouwstra)**](https://github.com/KiaraGrouwstra)
- Side Effect [**(@YawKar)**](https://github.com/YawKar)
- dram [**(@dramforever)**](https://github.com/dramforever)
- tomf [**(@tomfitzhenry)**](https://github.com/tomfitzhenry)
- Kamil Monicz [**(@Zaczero)**](https://github.com/Zaczero)
- Cosima Neidahl [**(@OPNA2608)**](https://github.com/OPNA2608)
- Siddhant Kumar [**(@siddhantk232)**](https://github.com/siddhantk232)
- Jens Petersen [**(@juhp)**](https://github.com/juhp)
- Johannes Kirschbauer [**(@hsjobeki)**](https://github.com/hsjobeki)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Artemis Tosini [**(@artemist)**](https://github.com/artemist)
- David McFarland [**(@corngood)**](https://github.com/corngood)
- Tucker Shea [**(@NoRePercussions)**](https://github.com/NoRePercussions)
- Connor Baker [**(@ConnorBaker)**](https://github.com/ConnorBaker)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Eveeifyeve [**(@Eveeifyeve)**](https://github.com/Eveeifyeve)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Graham Christensen [**(@grahamc)**](https://github.com/grahamc)
- Ilja [**(@iljah)**](https://github.com/iljah)
- Pol Dellaiera [**(@drupol)**](https://github.com/drupol)
- steelman [**(@steelman)**](https://github.com/steelman)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- JustAGuyTryingHisBest [**(@JustAGuyTryingHisBest)**](https://github.com/JustAGuyTryingHisBest)
- zowoq [**(@zowoq)**](https://github.com/zowoq)
- Agustín Covarrubias [**(@agucova)**](https://github.com/agucova)
- Sergei Trofimovich [**(@trofi)**](https://github.com/trofi)
- Bernardo Meurer [**(@lovesegfault)**](https://github.com/lovesegfault)
- Peter Bynum [**(@pkpbynum)**](https://github.com/pkpbynum)
- Amaan Qureshi [**(@amaanq)**](https://github.com/amaanq)
- Michael Hoang [**(@Enzime)**](https://github.com/Enzime)
- Michael Daniels [**(@mdaniels5757)**](https://github.com/mdaniels5757)
- Matthew Kenigsberg [**(@mkenigs)**](https://github.com/mkenigs)
- Shea Levy [**(@shlevy)**](https://github.com/shlevy)

# Release 2.34.1 (2026-03-08)

## Changes

- C API: Fix `EvalState` pointer passed to primop callbacks [#15300](https://github.com/NixOS/nix/pull/15300) [#15383](https://github.com/NixOS/nix/pull/15383)

  The `EvalState *` passed to C API primop callbacks was incorrectly pointing to
  the internal `nix::EvalState` rather than the C API wrapper struct. This caused
  a segfault when the callback used the pointer with C API functions such as
  `nix_alloc_value()`. The same issue affected `printValueAsJSON` and
  `printValueAsXML` callbacks on external values.

- Fix daemon not applying `FileTransferSettings` from `trusted-users` [#15408](https://github.com/NixOS/nix/pull/15408)

  Previously `nix-daemon` failed to apply settings for `libcurl` configuration configured by client connections from [`trusted-users`](@docroot@/command-ref/conf-file.md#conf-trusted-users). This was a pre-existing bug, which has been exacerbated by 2.34.0 moving more settings from the global `settings` into libcurl-specific `fileTransferSettings` (e.g. `netrc-file`, `http-connections` or `ssl-cert-file`). Note that the use of `trusted-users` is heavily discouraged unless you are fine with:

  > Adding a user to `trusted-users` is essentially equivalent to giving that user root access to the system.
  > For example, the user can access or replace store path contents that are critical for system security.

- Improve formatting of error messages and warnings [#15397](https://github.com/NixOS/nix/pull/15397)

# Release 2.34.2 (2026-03-20)

- S3: restore STS WebIdentity and ECS container credential providers [#15507](https://github.com/NixOS/nix/pull/15507)

  Nix 2.33 replaced the S3 backend's `aws-sdk-cpp` credential chain with a
  custom chain built on `aws-c-auth`. That chain omitted two providers,
  breaking S3 binary cache access in container workloads:

  - **STS WebIdentity** (`AWS_WEB_IDENTITY_TOKEN_FILE`, `AWS_ROLE_ARN`,
    `AWS_ROLE_SESSION_NAME`) — used by EKS IRSA, GitHub Actions OIDC, and
    any `sts:AssumeRoleWithWebIdentity` federation.
  - **ECS container metadata** (`AWS_CONTAINER_CREDENTIALS_RELATIVE_URI`,
    `AWS_CONTAINER_CREDENTIALS_FULL_URI`) — used by ECS tasks and EKS Pod
    Identity.

  The typical symptom was a misleading IMDS error
  (`Valid credentials could not be sourced by the IMDS provider`), because
  IMDS is the last provider tried after the correct one was skipped.

  Both providers are now part of the chain, ordered to match the
  pre-2.33 `DefaultAWSCredentialsProviderChain`:
  `Environment → SSO → Profile → STS WebIdentity → (ECS | IMDS)`.
  As in both the old and new AWS SDK default chains, ECS and IMDS are
  mutually exclusive: when container credential environment variables are
  set, IMDS is skipped.

