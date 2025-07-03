# Changes between Nix and Determinate Nix

This section lists the differences between upstream Nix 2.29 and Determinate Nix 3.7.0.<!-- differences -->

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

* Fix deep overrides by @edolstra in [DeterminateSystems/nix-src#108](https://github.com/DeterminateSystems/nix-src/pull/108)

* Fix eval caching for path flakes by @edolstra in [DeterminateSystems/nix-src#131](https://github.com/DeterminateSystems/nix-src/pull/131)

* nix store delete: Show why deletion fails by @edolstra in [DeterminateSystems/nix-src#130](https://github.com/DeterminateSystems/nix-src/pull/130)

* nix flake prefetch-inputs: Add by @edolstra in [DeterminateSystems/nix-src#127](https://github.com/DeterminateSystems/nix-src/pull/127)
