# Release 2.31.0 (2025-08-21)

- `build-cores = 0` now auto-detects CPU cores [#13402](https://github.com/NixOS/nix/pull/13402)

  When `build-cores` is set to `0`, Nix now automatically detects the number of available CPU cores and passes this value via `NIX_BUILD_CORES`, instead of passing `0` directly. This matches the behavior when `build-cores` is unset. This prevents the builder from having to detect the number of cores.

- Fix Git LFS SSH issues [#13337](https://github.com/NixOS/nix/issues/13337) [#13743](https://github.com/NixOS/nix/pull/13743)

  Fixed some outstanding issues with Git LFS and SSH.

  * Added support for `NIX_SSHOPTS`.
  * Properly use the parsed port from URL.
  * Better use of the response of `git-lfs-authenticate` to determine API endpoint when the API is not exposed on port 443.

- Add support for `user@address:port` syntax in store URIs [#7044](https://github.com/NixOS/nix/issues/7044) [#3425](https://github.com/NixOS/nix/pull/3425)

  It's now possible to specify the port used for SSH stores directly in the store URL in accordance with [RFC3986](https://datatracker.ietf.org/doc/html/rfc3986). Previously the only way to specify custom ports was via `ssh_config` or the `NIX_SSHOPTS` environment variable, because Nix incorrectly passed the port number together with the host name to the SSH executable.

  This change affects [store references](@docroot@/store/types/index.md#store-url-format) passed via the `--store` and similar flags in CLI as well as in the configuration for [remote builders](@docroot@/command-ref/conf-file.md#conf-builders). For example, the following store URIs now work:

  - `ssh://127.0.0.1:2222`
  - `ssh://[b573:6a48:e224:840b:6007:6275:f8f7:ebf3]:22`
  - `ssh-ng://[b573:6a48:e224:840b:6007:6275:f8f7:ebf3]:22`

- Represent IPv6 RFC4007 ZoneId literals in conformance with RFC6874 [#13445](https://github.com/NixOS/nix/pull/13445)

  Prior versions of Nix since [#4646](https://github.com/NixOS/nix/pull/4646) accepted [IPv6 scoped addresses](https://datatracker.ietf.org/doc/html/rfc4007) in URIs like [store references](@docroot@/store/types/index.md#store-url-format) in the textual representation with a literal percent character: `[fe80::1%18]`. This was ambiguous, because the the percent literal `%` is reserved by [RFC3986](https://datatracker.ietf.org/doc/html/rfc3986), since it's used to indicate percent encoding. Nix now requires that the percent `%` symbol is percent-encoded as `%25`. This implements [RFC6874](https://datatracker.ietf.org/doc/html/rfc6874), which defines the representation of zone identifiers in URIs. The example from above now has to be specified as `[fe80::1%2518]`.

- Use WAL mode for SQLite cache databases [#13800](https://github.com/NixOS/nix/pull/13800)

  Previously, Nix used SQLite's "truncate" mode for caches. However, this could cause a Nix process to block if another process was updating the cache. This was a problem for the flake evaluation cache in particular, since it uses long-running transactions. Thus, concurrent Nix commands operating on the same flake could be blocked for an unbounded amount of time. WAL mode avoids this problem.

  This change required updating the versions of the SQLite caches. For instance, `eval-cache-v5.sqlite` is now `eval-cache-v6.sqlite`.

- Enable parallel marking in bdwgc [#13708](https://github.com/NixOS/nix/pull/13708)

  Previously marking was done by only one thread, which takes a long time if the heap gets big. Enabling parallel marking speeds up evaluation a lot, for example (on a Ryzen 9 5900X 12-Core):

  * `nix search nixpkgs` from 24.3s to 18.9s.
  * Evaluating the `NixOS/nix/2.21.2` flake regression test from 86.1s to 71.2s.

- New command `nix flake prefetch-inputs` [#13565](https://github.com/NixOS/nix/pull/13565)

  This command fetches all inputs of a flake in parallel. This can be a lot faster than the serialized on-demand fetching during regular flake evaluation. The downside is that it may fetch inputs that aren't normally used.

- Add `warn-short-path-literals` setting [#13489](https://github.com/NixOS/nix/pull/13489)

  This setting, when enabled, causes Nix to emit warnings when encountering relative path literals that don't start with `.` or `/`, for instance suggesting that `foo/bar` should be rewritten to `./foo/bar`.

- When updating a lock, respect the input's lock file [#13437](https://github.com/NixOS/nix/pull/13437)

  For example, if a flake has a lock for `a` and `a/b`, and we change the flakeref for `a`, previously Nix would fetch the latest version of `b` rather than using the lock for `b` from `a`.

- Implement support for Git hashing with SHA-256 [#13543](https://github.com/NixOS/nix/pull/13543)

  The experimental support for [Git-hashing](@docroot@/development/experimental-features.md#xp-feature-git-hashing) store objects now also includes support for SHA-256, not just SHA-1, in line with upstream Git.

## Contributors

This release was made possible by the following 34 contributors:

- John Soo [**(@jsoo1)**](https://github.com/jsoo1)
- Alan Urmancheev [**(@alurm)**](https://github.com/alurm)
- Manse [**(@PedroManse)**](https://github.com/PedroManse)
- Pol Dellaiera [**(@drupol)**](https://github.com/drupol)
- DavHau [**(@DavHau)**](https://github.com/DavHau)
- Leandro Emmanuel Reina Kiperman [**(@kip93)**](https://github.com/kip93)
- h0nIg [**(@h0nIg)**](https://github.com/h0nIg)
- Philip Taron [**(@philiptaron)**](https://github.com/philiptaron)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Connor Baker [**(@ConnorBaker)**](https://github.com/ConnorBaker)
- kenji [**(@a-kenji)**](https://github.com/a-kenji)
- Oleksandr Knyshuk [**(@k1gen)**](https://github.com/k1gen)
- Maciej Krüger [**(@mkg20001)**](https://github.com/mkg20001)
- Justin Bailey [**(@jgbailey-well)**](https://github.com/jgbailey-well)
- Emily [**(@emilazy)**](https://github.com/emilazy)
- Volker Diels-Grabsch [**(@vog)**](https://github.com/vog)
- gustavderdrache [**(@gustavderdrache)**](https://github.com/gustavderdrache)
- Elliot Cameron [**(@de11n)**](https://github.com/de11n)
- Alexander V. Nikolaev [**(@avnik)**](https://github.com/avnik)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Matthew Kenigsberg [**(@mkenigs)**](https://github.com/mkenigs)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Cosima Neidahl [**(@OPNA2608)**](https://github.com/OPNA2608)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- m4dc4p [**(@m4dc4p)**](https://github.com/m4dc4p)
- Graham Christensen [**(@grahamc)**](https://github.com/grahamc)
- Jason Yundt [**(@Jayman2000)**](https://github.com/Jayman2000)
- Jens Petersen [**(@juhp)**](https://github.com/juhp)
- the-sun-will-rise-tomorrow [**(@the-sun-will-rise-tomorrow)**](https://github.com/the-sun-will-rise-tomorrow)
- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- AGawas [**(@aln730)**](https://github.com/aln730)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Dmitry Bogatov [**(@KAction)**](https://github.com/KAction)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Philipp Otterbein
