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

  ### [`lint-url-literals`](@docroot@/command-ref/conf-file.md#conf-lint-url-literals)

  The `no-url-literals` experimental feature has been stabilised and replaced with a new [`lint-url-literals`](@docroot@/command-ref/conf-file.md#conf-lint-url-literals) setting.

  To migrate from the experimental feature, replace:
  ```
  experimental-features = no-url-literals
  ```
  with:
  ```
  lint-url-literals = fatal
  ```

  ### [`lint-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-short-path-literals)

  The [`warn-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-warn-short-path-literals) boolean setting has been deprecated and replaced with [`lint-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-short-path-literals).

  To migrate, replace:
  ```
  warn-short-path-literals = true
  ```
  with:
  ```
  lint-short-path-literals = warn
  ```

  ### [`lint-absolute-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-absolute-path-literals)

  A new [`lint-absolute-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-absolute-path-literals) setting has been added to control handling of absolute path literals (paths starting with `/`) and home path literals (paths starting with `~/`).

  ### Setting values

  All three settings accept three values:
  - `ignore`: Allow the feature without emitting any diagnostic (default)
  - `warn`: Emit a warning when the feature is used
  - `fatal`: Treat the feature as a parse error

  The defaults may change in future versions.

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

## C API Changes

- New store API methods [#14766](https://github.com/NixOS/nix/pull/14766)

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

- S3 binary caches now use virtual-hosted-style addressing by default [#15208](https://github.com/NixOS/nix/issues/15208)

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

## Miscellaneous changes

- Content-Encoding decompression is now handled by libcurl [#14324](https://github.com/NixOS/nix/issues/14324) [#15336](https://github.com/NixOS/nix/pull/15336)

  Transparent decompression of HTTP downloads specifying `Content-Encoding` header now uses libcurl. This adds support for previously advertised, but not supported `deflate` encoding as well as deprecated `x-gzip` alias.
  Non-standard `xz`, `bzip2` encodings that were previously advertised are no longer supported, as they do not commonly appear in the wild and should not be sent by compliant servers.

  `br`, `zstd`, `gzip` continue to be supported. Distro packaging should ensure that the `libcurl` dependency is linked against required libraries to support these encodings. By default, the build system now requires libcurl >= 8.17.0, which is not known to have issues around [pausing and decompression](https://github.com/curl/curl/issues/16280).

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
