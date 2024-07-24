---
synopsis: Other changes
---

- [#9063](https://github.com/NixOS/nix/pull/9063): introduce `libnixflake` and move flakes-specific code there (Nix is internally composed of a set of libraries, and this better reflects the architecture wrt flakes)
- [#10852](https://github.com/NixOS/nix/pull/10852): make Nix commands respond better to interruption - Ctrl+C in the terminal
- [#10853](https://github.com/NixOS/nix/pull/10853): fix docs of `builtins.importNative`
- [#10858](https://github.com/NixOS/nix/pull/10858): `flake check`: Recognize well known `homeModule`/`homeModules` attribute
- [#10865](https://github.com/NixOS/nix/pull/10865): Update dependencies to Nixpkgs 24.05 (when using the `nix` flake), and support bdwgc 8.2.6 ([#11141](https://github.com/NixOS/nix/issues/11141), [#10880](https://github.com/NixOS/nix/pull/10880))
- [#10883](https://github.com/NixOS/nix/pull/10883): Use `TMP` instead of `XDG_RUNTIME_DIR`
- [#10994](https://github.com/NixOS/nix/pull/10994): fix minor bug in elided item counts when printing values lazily
- [#10988](https://github.com/NixOS/nix/pull/10988): restore `commit-lockfile-summary` alias
- [#10941](https://github.com/NixOS/nix/pull/10941): invalid derivation name now causes an actionable error message
- Changes to the interaction between Nix's I/O coroutines and the garbage collector: 
- [#10878](https://github.com/NixOS/nix/pull/10878): allow `ipc-sysv*` in the Darwin build sandbox
- [#11031](https://github.com/NixOS/nix/pull/11031): fix Darwin build sandbox
- [#10907](https://github.com/NixOS/nix/pull/10907): use opaque struct instead of `void *` in the C API
- [#10947](https://github.com/NixOS/nix/issues/10947): fix evaluation cache accidentally persisting disallowed IFD errors
- [#11020](https://github.com/NixOS/nix/pull/11020): enable fetch and eval caching for tarballs
- [#11041](https://github.com/NixOS/nix/pull/11041): add discovered attribute paths to the `--show-trace` error trace in `nix-build`, `nix-env`, OfBorg, and other callers of `getDerivations`
- [#11056](https://github.com/NixOS/nix/pull/11056): `s3` store now uses system defined proxy settings
- [#11077](https://github.com/NixOS/nix/pull/11077): support hardlinks in tarballs
- [#11100](https://github.com/NixOS/nix/pull/11100): pretty print values consistently regardless of prior thunk state
- [#11086](https://github.com/NixOS/nix/pull/11086): fix loss of evaluation cache additions in `nix env run`, `nix shell`, `nix develop`, and `nix fmt`
- [#11149](https://github.com/NixOS/nix/pull/11149): report GC time and number of GC cycles in `NIX_SHOW_STATS=1` report
- [#11142](https://github.com/NixOS/nix/pull/11142): aliased options can now also be passed as flags, just like their "normal" counterparts, e.g. `--build-max-jobs` now works
- [#11043](https://github.com/NixOS/nix/pull/11043): `assert a == b; e` now reports some detail about why `a` and `b` are different when they are
- [#11159](https://github.com/NixOS/nix/pull/11159): don't crash a nix-daemon worker process when the client disconnects
- Stability improvements and fixes [#10861](https://github.com/NixOS/nix/pull/10861), [#10865](https://github.com/NixOS/nix/pull/10865), [#10918](https://github.com/NixOS/nix/pull/10918), [#10916](https://github.com/NixOS/nix/pull/10916), [#10884](https://github.com/NixOS/nix/pull/10884), [#10943](https://github.com/NixOS/nix/pull/10943), [#11019](https://github.com/NixOS/nix/pull/11019), [#11122](https://github.com/NixOS/nix/pull/11122), [#11117](https://github.com/NixOS/nix/pull/11117)
- User documentation improvements [#10888](https://github.com/NixOS/nix/pull/10888), [#10966](https://github.com/NixOS/nix/pull/10966), [#10974](https://github.com/NixOS/nix/pull/10974), [#10997](https://github.com/NixOS/nix/pull/10997), [#11013](https://github.com/NixOS/nix/pull/11013), [#11059](https://github.com/NixOS/nix/pull/11059), [#11119](https://github.com/NixOS/nix/pull/11119), [#11116](https://github.com/NixOS/nix/pull/11116), [#11061](https://github.com/NixOS/nix/pull/11061), [#11102](https://github.com/NixOS/nix/pull/11102)
- BSD support: [#10896](https://github.com/NixOS/nix/pull/10896) [#11022](https://github.com/NixOS/nix/pull/11022) [#11156](https://github.com/NixOS/nix/pull/11156)
- Windows support: [#10769](https://github.com/NixOS/nix/pull/10769), [#10975](https://github.com/NixOS/nix/pull/10975) [#11153](https://github.com/NixOS/nix/pull/11153)
- Portability: [#7048](https://github.com/NixOS/nix/pull/7048) [#11090](https://github.com/NixOS/nix/pull/11090)
- Installer improvements [#10902](https://github.com/NixOS/nix/pull/10902)
- Performance improvements [#10853](https://github.com/NixOS/nix/pull/10853), [#10854](https://github.com/NixOS/nix/pull/10854), [#11082](https://github.com/NixOS/nix/pull/11082), [#11092](https://github.com/NixOS/nix/pull/11092), [#11113](https://github.com/NixOS/nix/pull/11113)

Contributor experience improvements:

Use Meson to build Nix (nearing completion) [#10855](https://github.com/NixOS/nix/pull/10855) [#10904](https://github.com/NixOS/nix/pull/10904) [#10908](https://github.com/NixOS/nix/pull/10908) [#10914](https://github.com/NixOS/nix/pull/10914) [#10933](https://github.com/NixOS/nix/pull/10933) [#10936](https://github.com/NixOS/nix/pull/10936) [#10954](https://github.com/NixOS/nix/pull/10954) [#10955](https://github.com/NixOS/nix/pull/10955) [#10967](https://github.com/NixOS/nix/pull/10967) [#10963](https://github.com/NixOS/nix/pull/10963) [#10973](https://github.com/NixOS/nix/pull/10973) [#11034](https://github.com/NixOS/nix/pull/11034) [#11054](https://github.com/NixOS/nix/pull/11054) [#11055](https://github.com/NixOS/nix/pull/11055) [#11064](https://github.com/NixOS/nix/pull/11064) [#11060](https://github.com/NixOS/nix/pull/11060) [#11155](https://github.com/NixOS/nix/pull/11155)
- Testing improvements [#10864](https://github.com/NixOS/nix/pull/10864), [#10903](https://github.com/NixOS/nix/pull/10903), [#10874](https://github.com/NixOS/nix/pull/10874), [#10922](https://github.com/NixOS/nix/pull/10922), [#11006](https://github.com/NixOS/nix/pull/11006), [#11110](https://github.com/NixOS/nix/pull/11110), [#10931](https://github.com/NixOS/nix/pull/10931), [#11123](https://github.com/NixOS/nix/pull/11123)
  - [#10603](https://github.com/NixOS/nix/pull/10603): We now evaluate a set of flakes in CI
  - [#10922](https://github.com/NixOS/nix/pull/10922): The functional test suite is now run in both in the build sandbox and in a NixOS environment
- CI improvements [#10929](https://github.com/NixOS/nix/pull/10929) [#10999](https://github.com/NixOS/nix/pull/10999) [#11009](https://github.com/NixOS/nix/pull/11009) [#11065](https://github.com/NixOS/nix/pull/11065) [#11071](https://github.com/NixOS/nix/pull/11071)
- Contributor documentation improvements [#10869](https://github.com/NixOS/nix/pull/10869), [#9871](https://github.com/NixOS/nix/pull/9871), [#10960](https://github.com/NixOS/nix/pull/10960), [#11147](https://github.com/NixOS/nix/pull/11147)
- Error message improvements: [#11050](https://github.com/NixOS/nix/pull/11050) [#11154](https://github.com/NixOS/nix/pull/11154)
- Cleaning up the Settings system (`nix.conf` and related architectural cleanups): [#10913](https://github.com/NixOS/nix/pull/10913), [#10951](https://github.com/NixOS/nix/pull/10951), [#11007](https://github.com/NixOS/nix/pull/11007), [#11108](https://github.com/NixOS/nix/pull/11108), [#11014](https://github.com/NixOS/nix/pull/11014), [#11109](https://github.com/NixOS/nix/pull/11109), [#11112](https://github.com/NixOS/nix/pull/11112)
- Other cleanups and refactors [#10857](https://github.com/NixOS/nix/pull/10857) [#10935](https://github.com/NixOS/nix/pull/10935) [#10873](https://github.com/NixOS/nix/pull/10873) [#10745](https://github.com/NixOS/nix/pull/10745) [#10961](https://github.com/NixOS/nix/pull/10961) [#10962](https://github.com/NixOS/nix/pull/10962) [#10972](https://github.com/NixOS/nix/pull/10972) [#11018](https://github.com/NixOS/nix/pull/11018) [#11035](https://github.com/NixOS/nix/pull/11035) [#11037](https://github.com/NixOS/nix/pull/11037) [#11081](https://github.com/NixOS/nix/pull/11081) [#11089](https://github.com/NixOS/nix/pull/11089) [#11093](https://github.com/NixOS/nix/pull/11093) [#11114](https://github.com/NixOS/nix/pull/11114) [#11103](https://github.com/NixOS/nix/pull/11103) [#11126](https://github.com/NixOS/nix/pull/11126) [#11125](https://github.com/NixOS/nix/pull/11125) [#11120](https://github.com/NixOS/nix/pull/11120)
- Scheduler/builder refactoring [#11005](https://github.com/NixOS/nix/pull/11005)
- [#11011](https://github.com/NixOS/nix/pull/11011): enable `-Werror=unused-result`

