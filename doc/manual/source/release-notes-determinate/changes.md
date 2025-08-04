# Changes between Nix and Determinate Nix

This section lists the differences between upstream Nix 2.30 and Determinate Nix 3.8.5.<!-- differences -->

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

* Initial Lazy Trees support has been merged, but remains off by default. ([DeterminateSystems/nix-src#27](https://github.com/DeterminateSystems/nix-src/pull/27), [DeterminateSystems/nix-src#56](https://github.com/DeterminateSystems/nix-src/pull/56))

<!-- Determinate Nix version 3.5.2 -->

* Tell users a source is corrupted ("cannot read file from tarball: Truncated tar archive detected while reading data"), improving over the previous 'cannot read file from tarball' error by @edolstra in [DeterminateSystems/nix-src#64](https://github.com/DeterminateSystems/nix-src/pull/64)
<!-- Determinate Nix version 3.6.0 -->

* Emit warnings when using import-from-derivation by setting the `trace-import-from-derivation` option to `true` by @gustavderdrache in [DeterminateSystems/nix-src#70](https://github.com/DeterminateSystems/nix-src/pull/70)
<!-- Determinate Nix version 3.6.1 -->

<!-- Determinate Nix version 3.6.2 -->

* Faster `nix store copy-sigs` by @edolstra in [DeterminateSystems/nix-src#80](https://github.com/DeterminateSystems/nix-src/pull/80)

* Document how to replicate nix-store --query --deriver with the nix cli by @grahamc in [DeterminateSystems/nix-src#82](https://github.com/DeterminateSystems/nix-src/pull/82)

* Garbage collector: Keep going even when encountering an undeletable file by @edolstra in [DeterminateSystems/nix-src#83](https://github.com/DeterminateSystems/nix-src/pull/83)

* nix profile: Replace ε and ∅ with descriptive English words by @grahamc in [DeterminateSystems/nix-src#81](https://github.com/DeterminateSystems/nix-src/pull/81)

* Call out that `--keep-failed` with remote builders will keep the failed build directory on that builder by @cole-h in [DeterminateSystems/nix-src#85](https://github.com/DeterminateSystems/nix-src/pull/85)
<!-- Determinate Nix version 3.6.3 revoked -->

<!-- Determinate Nix version 3.6.4 revoked -->

<!-- Determinate Nix version 3.6.5 -->

* When remote building with --keep-failed, only show "you can rerun" message if the derivation's platform is supported on this machine by @cole-h in [DeterminateSystems/nix-src#87](https://github.com/DeterminateSystems/nix-src/pull/87)

* Indicate that sandbox-paths specifies a missing file in the corresponding error message. by @cole-h in [DeterminateSystems/nix-src#88](https://github.com/DeterminateSystems/nix-src/pull/88)

* Use 'published' release type to avoid double uploads by @gustavderdrache in [DeterminateSystems/nix-src#90](https://github.com/DeterminateSystems/nix-src/pull/90)

* Render lazy tree paths in messages withouth the/nix/store/hash... prefix in substituted source trees by @edolstra in [DeterminateSystems/nix-src#91](https://github.com/DeterminateSystems/nix-src/pull/91)

* Use FlakeHub inputs by @lucperkins in [DeterminateSystems/nix-src#89](https://github.com/DeterminateSystems/nix-src/pull/89)

* Proactively cache more flake inputs and fetches by @edolstra in [DeterminateSystems/nix-src#93](https://github.com/DeterminateSystems/nix-src/pull/93)

* Fix: register extra builtins just once by @edolstra in [DeterminateSystems/nix-src#97](https://github.com/DeterminateSystems/nix-src/pull/97)

* Fix: Make the S3 test more robust by @gustavderdrache in [DeterminateSystems/nix-src#101](https://github.com/DeterminateSystems/nix-src/pull/101)

* Fix the link to `builders-use-substitutes` documentation for `builders` by @lucperkins in [DeterminateSystems/nix-src#102](https://github.com/DeterminateSystems/nix-src/pull/102)

* Improve error messages that use the hypothetical future tense of "will" by @lucperkins in [DeterminateSystems/nix-src#92](https://github.com/DeterminateSystems/nix-src/pull/92)

* Improve caching of inputs in dry-run mode by @edolstra in [DeterminateSystems/nix-src#98](https://github.com/DeterminateSystems/nix-src/pull/98)

<!-- Determinate Nix version 3.6.6 -->

<!-- Determinate Nix version 3.6.7 -->

<!-- Determinate Nix version 3.6.8 -->

* Fix fetchToStore() caching with --impure, improve testing by @edolstra in [DeterminateSystems/nix-src#117](https://github.com/DeterminateSystems/nix-src/pull/117)

* Add lazy-locks setting by @edolstra in [DeterminateSystems/nix-src#113](https://github.com/DeterminateSystems/nix-src/pull/113)

* Sync 2.29.1 by @edolstra in [DeterminateSystems/nix-src#124](https://github.com/DeterminateSystems/nix-src/pull/124)

* Release v3.6.7 by @github-actions in [DeterminateSystems/nix-src#126](https://github.com/DeterminateSystems/nix-src/pull/126)

<!-- Determinate Nix version 3.7.0 -->

* Overriding deeply-nested transitive flake inputs now works, by @edolstra in [DeterminateSystems/nix-src#108](https://github.com/DeterminateSystems/nix-src/pull/108)

* `nix store delete` now explains why deletion fails by @edolstra in [DeterminateSystems/nix-src#130](https://github.com/DeterminateSystems/nix-src/pull/130)

* New command: `nix flake prefetch-inputs` for improved CI performance, by @edolstra in [DeterminateSystems/nix-src#127](https://github.com/DeterminateSystems/nix-src/pull/127)

<!-- Determinate Nix version 3.8.0 -->

* nix flake check: Skip substitutable derivations by @edolstra in [DeterminateSystems/nix-src#134](https://github.com/DeterminateSystems/nix-src/pull/134)

* lockFlake(): When updating a lock, respect the input's lock file by @edolstra in [DeterminateSystems/nix-src#137](https://github.com/DeterminateSystems/nix-src/pull/137)

<!-- Determinate Nix version 3.8.1 -->

* Address ifdef problem with macOS/BSD sandboxing by @gustavderdrache in [DeterminateSystems/nix-src#142](https://github.com/DeterminateSystems/nix-src/pull/142)

<!-- Determinate Nix version 3.8.2 -->

* ci: don't run the full test suite for x86_64-darwin by @grahamc in [DeterminateSystems/nix-src#144](https://github.com/DeterminateSystems/nix-src/pull/144)

* Try publishing the manual again by @grahamc in [DeterminateSystems/nix-src#145](https://github.com/DeterminateSystems/nix-src/pull/145)

<!-- Determinate Nix version 3.8.3 -->

* Add an `external-builders` experimental feature by @cole-h in [DeterminateSystems/nix-src#141](https://github.com/DeterminateSystems/nix-src/pull/141)

* Add support for external builders by @edolstra in [DeterminateSystems/nix-src#78](https://github.com/DeterminateSystems/nix-src/pull/78)

<!-- Determinate Nix version 3.8.4 -->

* Revert "Use WAL mode for SQLite cache databases" by @grahamc in [DeterminateSystems/nix-src#155](https://github.com/DeterminateSystems/nix-src/pull/155)

<!-- Determinate Nix version 3.8.5 -->

* Tab completing arguments to Nix avoids network access. [DeterminateSystems/nix-src#161](https://github.com/DeterminateSystems/nix-src/pull/161)

* Nix on ZFS no longer stalls for multiple seconds at a time [DeterminateSystems/nix-src#158](https://github.com/DeterminateSystems/nix-src/pull/158)

* Importing Nixpkgs and other tarballs to the cache 2-4x faster [DeterminateSystems/nix-src#149](https://github.com/DeterminateSystems/nix-src/pull/149)

* Adding paths to the store is significantly faster [DeterminateSystems/nix-src#162](https://github.com/DeterminateSystems/nix-src/pull/162)
