# Changes between Nix and Determinate Nix

This section lists the differences between upstream Nix 2.31 and Determinate Nix 3.11.2.<!-- differences -->

* In Determinate Nix, flakes are stable. You no longer need to enable the `flakes` experimental feature.

* In Determinate Nix, the new Nix CLI (i.e. the `nix` command) is stable. You no longer need to enable the `nix-command` experimental feature.

* Determinate Nix has a setting [`json-log-path`](@docroot@/command-ref/conf-file.md#conf-json-log-path) to send a copy of all Nix log messages (in JSON format) to a file or Unix domain socket.

* Determinate Nix has made `nix profile install` an alias to `nix profile add`, a more symmetrical antonym of `nix profile remove`.

* `nix-channel` and `channel:` url syntax (like `channel:nixos-24.11`) is deprecated, see: https://github.com/DeterminateSystems/nix-src/issues/34

* Using indirect flake references and implicit inputs is deprecated, see: https://github.com/DeterminateSystems/nix-src/issues/37

* Warnings around "dirty trees" are updated to reduce "dirty" jargon, and now refers to "uncommitted changes".

<!-- Determinate Nix version 3.4.2 -->

<!-- Determinate Nix version 3.5.0 -->

<!-- Determinate Nix version 3.5.1 -->

* `nix upgrade-nix` is now inert, and suggests using `determinate-nixd upgrade` -- [DeterminateSystems/nix-src#55](https://github.com/DeterminateSystems/nix-src/pull/55)

* Lazy Trees support has been merged. ([DeterminateSystems/nix-src#27](https://github.com/DeterminateSystems/nix-src/pull/27), [DeterminateSystems/nix-src#56](https://github.com/DeterminateSystems/nix-src/pull/56))

<!-- Determinate Nix version 3.5.2 -->

<!-- Determinate Nix version 3.6.0 -->

<!-- Determinate Nix version 3.6.1 -->

<!-- Determinate Nix version 3.6.2 -->

* Faster `nix store copy-sigs` by @edolstra in [DeterminateSystems/nix-src#80](https://github.com/DeterminateSystems/nix-src/pull/80)

* Document how to replicate nix-store --query --deriver with the nix cli by @grahamc in [DeterminateSystems/nix-src#82](https://github.com/DeterminateSystems/nix-src/pull/82)

* nix profile: Replace ε and ∅ with descriptive English words by @grahamc in [DeterminateSystems/nix-src#81](https://github.com/DeterminateSystems/nix-src/pull/81)

* Call out that `--keep-failed` with remote builders will keep the failed build directory on that builder by @cole-h in [DeterminateSystems/nix-src#85](https://github.com/DeterminateSystems/nix-src/pull/85)
<!-- Determinate Nix version 3.6.3 revoked -->

<!-- Determinate Nix version 3.6.4 revoked -->

<!-- Determinate Nix version 3.6.5 -->

* When remote building with --keep-failed, only show "you can rerun" message if the derivation's platform is supported on this machine by @cole-h in [DeterminateSystems/nix-src#87](https://github.com/DeterminateSystems/nix-src/pull/87)

* Indicate that sandbox-paths specifies a missing file in the corresponding error message. by @cole-h in [DeterminateSystems/nix-src#88](https://github.com/DeterminateSystems/nix-src/pull/88)

* Use FlakeHub inputs by @lucperkins in [DeterminateSystems/nix-src#89](https://github.com/DeterminateSystems/nix-src/pull/89)

* Proactively cache more flake inputs and fetches by @edolstra in [DeterminateSystems/nix-src#93](https://github.com/DeterminateSystems/nix-src/pull/93)

* Fix the link to `builders-use-substitutes` documentation for `builders` by @lucperkins in [DeterminateSystems/nix-src#102](https://github.com/DeterminateSystems/nix-src/pull/102)

* Improve caching of inputs in dry-run mode by @edolstra in [DeterminateSystems/nix-src#98](https://github.com/DeterminateSystems/nix-src/pull/98)

<!-- Determinate Nix version 3.6.6 -->

<!-- Determinate Nix version 3.6.7 -->

<!-- Determinate Nix version 3.6.8 -->

* Fix fetchToStore() caching with --impure, improve testing by @edolstra in [DeterminateSystems/nix-src#117](https://github.com/DeterminateSystems/nix-src/pull/117)

* Add lazy-locks setting by @edolstra in [DeterminateSystems/nix-src#113](https://github.com/DeterminateSystems/nix-src/pull/113)

<!-- Determinate Nix version 3.7.0 -->

* `nix store delete` now explains why deletion fails by @edolstra in [DeterminateSystems/nix-src#130](https://github.com/DeterminateSystems/nix-src/pull/130)

<!-- Determinate Nix version 3.8.0 -->

* nix flake check: Skip substitutable derivations by @edolstra in [DeterminateSystems/nix-src#134](https://github.com/DeterminateSystems/nix-src/pull/134)

<!-- Determinate Nix version 3.8.1 -->

* Address ifdef problem with macOS/BSD sandboxing by @gustavderdrache in [DeterminateSystems/nix-src#142](https://github.com/DeterminateSystems/nix-src/pull/142)

<!-- Determinate Nix version 3.8.2 -->

* ci: don't run the full test suite for x86_64-darwin by @grahamc in [DeterminateSystems/nix-src#144](https://github.com/DeterminateSystems/nix-src/pull/144)

<!-- Determinate Nix version 3.8.3 -->

* Add an `external-builders` experimental feature [DeterminateSystems/nix-src#141](https://github.com/DeterminateSystems/nix-src/pull/141),
[DeterminateSystems/nix-src#78](https://github.com/DeterminateSystems/nix-src/pull/78)

<!-- Determinate Nix version 3.8.4 -->

<!-- Determinate Nix version 3.8.5 -->

* Tab completing arguments to Nix avoids network access [DeterminateSystems/nix-src#161](https://github.com/DeterminateSystems/nix-src/pull/161)

* Importing Nixpkgs and other tarballs to the cache is 2-4x faster [DeterminateSystems/nix-src#149](https://github.com/DeterminateSystems/nix-src/pull/149)

* Adding paths to the store is significantly faster [DeterminateSystems/nix-src#162](https://github.com/DeterminateSystems/nix-src/pull/162)

<!-- Determinate Nix version 3.8.6 -->

<!-- Determinate Nix version 3.9.0 -->

* Build-time flake inputs [DeterminateSystems/nix-src#49](https://github.com/DeterminateSystems/nix-src/pull/49)

<!-- Determinate Nix version 3.9.1 -->

* The default `nix flake init` template is much more useful [DeterminateSystems/nix-src#180](https://github.com/DeterminateSystems/nix-src/pull/180)

<!-- Determinate Nix version 3.10.0 -->

<!-- Determinate Nix version 3.10.1 -->


<!-- Determinate Nix version 3.11.0 -->

* Multithreaded evaluation support [DeterminateSystems/nix-src#125](https://github.com/DeterminateSystems/nix-src/pull/125)

<!-- Determinate Nix version 3.11.1 -->


<!-- Determinate Nix version 3.11.2 -->

* Fix flake registry ignoring `dir` parameter by @cole-h in [DeterminateSystems/nix-src#196](https://github.com/DeterminateSystems/nix-src/pull/196)

* Pass `dir` in extraAttrs when overriding the registry by @cole-h in [DeterminateSystems/nix-src#199](https://github.com/DeterminateSystems/nix-src/pull/199)

* The JSON output generated by `nix develop --profile` is now versioned [DeterminateSystems/nix-src#200](https://github.com/DeterminateSystems/nix-src/pull/200)
