# Release 2.30.0 (2025-07-07)

## Backward-incompatible changes and deprecations

- [`build-dir`] no longer defaults to `$TMPDIR`

  The directory in which temporary build directories are created no longer defaults
  to `TMPDIR` or `/tmp`, to avoid builders making their directories
  world-accessible. This behavior allowed escaping the build sandbox and can
  cause build impurities even when not used maliciously. We now default to `builds`
  in `NIX_STATE_DIR` (which is `/nix/var/nix/builds` in the default configuration).

- Deprecate manually making structured attrs using the `__json` attribute [#13220](https://github.com/NixOS/nix/pull/13220)

  The proper way to create a derivation using [structured attrs] in the Nix language is by using `__structuredAttrs = true` with [`builtins.derivation`].
  However, by exploiting how structured attrs are implementated, it has also been possible to create them by setting the `__json` environment variable to a serialized JSON string.
  This sneaky alternative method is now deprecated, and may be disallowed in future versions of Nix.

  [structured attrs]: @docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs
  [`builtins.derivation`]: @docroot@/language/builtins.html#builtins-derivation

- Rename `nix profile install` to [`nix profile add`] [#13224](https://github.com/NixOS/nix/pull/13224)

  The command `nix profile install` has been renamed to [`nix profile add`] (though the former is still available as an alias). This is because the verb "add" is a better antonym for the verb "remove" (i.e. `nix profile remove`). Nix also does not have install hooks or general behavior often associated with "installing".

## Performance improvements

This release has a number performance improvements, in particular:

- Reduce the size of value from 24 to 16 bytes [#13407](https://github.com/NixOS/nix/pull/13407)

  This shaves off a very significant amount of memory used for evaluation (~20% percent reduction in maximum heap size and ~17% in total bytes).

## Features

- Add [stack sampling evaluation profiler] [#13220](https://github.com/NixOS/nix/pull/13220)

  The Nix evaluator now supports [stack sampling evaluation profiling](@docroot@/advanced-topics/eval-profiler.md) via the [`--eval-profiler flamegraph`] setting.
  It outputs collapsed call stack information to the file specified by
  [`--eval-profile-file`] (`nix.profile` by default) in a format directly consumable
  by `flamegraph.pl` and compatible tools like [speedscope](https://speedscope.app/).
  Sampling frequency can be configured via [`--eval-profiler-frequency`] (99 Hz by default).

  Unlike the existing [`--trace-function-calls`], this profiler includes the name of the function
  being called when it's available.

- [`nix repl`] prints which variables were loaded [#11406](https://github.com/NixOS/nix/pull/11406)

  Instead of `Added <n> variables` it now prints the first 10 variables that were added to the global scope.

- `nix flake archive`: Add [`--no-check-sigs`] option [#13277](https://github.com/NixOS/nix/pull/13277)

  This is useful when using [`nix flake archive`] with the destination set to a remote store.

- Emit warnings for IFDs with [`trace-import-from-derivation`] option [#13279](https://github.com/NixOS/nix/pull/13279)

  While we have the setting [`allow-import-from-derivation`] to deny import-from-derivation (IFD), sometimes users would like to observe IFDs during CI processes to gradually phase out the idiom. The new setting `trace-import-from-derivation`, when set, logs a simple warning to the console.

- `json-log-path` setting [#13003](https://github.com/NixOS/nix/pull/13003)

  New setting [`json-log-path`] that sends a copy of all Nix log messages (in JSON format) to a file or Unix domain socket.

- Non-flake inputs now contain a `sourceInfo` attribute [#13164](https://github.com/NixOS/nix/issues/13164) [#13170](https://github.com/NixOS/nix/pull/13170)

  Flakes have always had a `sourceInfo` attribute which describes the source of the flake.
  The `sourceInfo.outPath` is often identical to the flake's `outPath`. However, it can differ when the flake is located in a subdirectory of its source.

  Non-flake inputs (i.e. inputs with [`flake = false`]) can also be located at some path _within_ a wider source.
  This usually happens when defining a relative path input within the same source as the parent flake, e.g. `inputs.foo.url = ./some-file.nix`.
  Such relative inputs will now inherit their parent's `sourceInfo`.

  This also means it is now possible to use `?dir=subdir` on non-flake inputs.

  This iterates on the work done in 2.26 to improve relative path support ([#10089](https://github.com/NixOS/nix/pull/10089)),
  and resolves a regression introduced in 2.28 relating to nested relative path inputs ([#13164](https://github.com/NixOS/nix/issues/13164)).

## Miscellaneous changes

- [`builtins.sort`] uses PeekSort [#12623](https://github.com/NixOS/nix/pull/12623)

  Previously it used libstdc++'s `std::stable_sort()`. However, that implementation is not reliable if the user-supplied comparison function is not a strict weak ordering.

- Revert incomplete closure mixed download and build feature [#77](https://github.com/NixOS/nix/issues/77) [#12628](https://github.com/NixOS/nix/issues/12628) [#13176](https://github.com/NixOS/nix/pull/13176)

  Since Nix 1.3 ([commit `299141e`] in 2013) Nix has attempted to mix together upstream fresh builds and downstream substitutions when remote substuters contain an "incomplete closure" (have some store objects, but not the store objects they reference).
  This feature is now removed.

  In the worst case, removing this feature could cause more building downstream, but it should not cause outright failures, since this is not happening for opaque store objects that we don't know how to build if we decide not to substitute.
  In practice, however, we doubt even more building is very likely to happen.
  Remote stores that are missing dependencies in arbitrary ways (e.g. corruption) don't seem to be very common.

  On the contrary, when remote stores fail to implement the [closure property](@docroot@/store/store-object.md#closure-property), it is usually an *intentional* choice on the part of the remote store, because it wishes to serve as an "overlay" store over another store, such as `https://cache.nixos.org`.
  If an "incomplete closure" is encountered in that situation, the right fix is not to do some sort of "franken-building" as this feature implemented, but instead to make sure both substituters are enabled in the settings.

  (In the future, we should make it easier for remote stores to indicate this to clients, to catch settings that won't work in general before a missing dependency is actually encountered.)

## Contributors

This release was made possible by the following 32 contributors:

- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Egor Konovalov [**(@egorkonovalov)**](https://github.com/egorkonovalov)
- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- Graham Christensen [**(@grahamc)**](https://github.com/grahamc)
- gustavderdrache [**(@gustavderdrache)**](https://github.com/gustavderdrache)
- Gwenn Le Bihan [**(@gwennlbh)**](https://github.com/gwennlbh)
- h0nIg [**(@h0nIg)**](https://github.com/h0nIg)
- Jade Masker [**(@donottellmetonottellyou)**](https://github.com/donottellmetonottellyou)
- jayeshv [**(@jayeshv)**](https://github.com/jayeshv)
- Jeremy Fleischman [**(@jfly)**](https://github.com/jfly)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Jonas Chevalier [**(@zimbatm)**](https://github.com/zimbatm)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- kstrafe [**(@kstrafe)**](https://github.com/kstrafe)
- Luc Perkins [**(@lucperkins)**](https://github.com/lucperkins)
- Matt Sturgeon [**(@MattSturgeon)**](https://github.com/MattSturgeon)
- Nikita Krasnov [**(@synalice)**](https://github.com/synalice)
- Peder Bergebakken Sundt [**(@pbsds)**](https://github.com/pbsds)
- pennae [**(@pennae)**](https://github.com/pennae)
- Philipp Otterbein
- Pol Dellaiera [**(@drupol)**](https://github.com/drupol)
- PopeRigby [**(@poperigby)**](https://github.com/poperigby)
- Raito Bezarius
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Samuli Thomasson [**(@SimSaladin)**](https://github.com/SimSaladin)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Seth Flynn [**(@getchoo)**](https://github.com/getchoo)
- Stefan Boca [**(@stefanboca)**](https://github.com/stefanboca)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Tristan Ross [**(@RossComputerGuy)**](https://github.com/RossComputerGuy)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- Vladimír Čunát [**(@vcunat)**](https://github.com/vcunat)
- Wolfgang Walther [**(@wolfgangwalther)**](https://github.com/wolfgangwalther)

<!-- markdown links -->
[stack sampling evaluation profiler]: @docroot@/advanced-topics/eval-profiler.md
[`--eval-profiler`]: @docroot@/command-ref/conf-file.md#conf-eval-profiler
[`--eval-profiler flamegraph`]: @docroot@/command-ref/conf-file.md#conf-eval-profiler
[`--trace-function-calls`]: @docroot@/command-ref/conf-file.md#conf-trace-function-calls
[`--eval-profile-file`]: @docroot@/command-ref/conf-file.md#conf-eval-profile-file
[`--eval-profiler-frequency`]: @docroot@/command-ref/conf-file.md#conf-eval-profiler-frequency
[`build-dir`]: @docroot@/command-ref/conf-file.md#conf-build-dir
[`nix profile add`]: @docroot@/command-ref/new-cli/nix3-profile-add.md
[`nix repl`]: @docroot@/command-ref/new-cli/nix3-repl.md
[`nix flake archive`]: @docroot@/command-ref/new-cli/nix3-flake-archive.md
[`json-log-path`]: @docroot@/command-ref/conf-file.md#conf-json-log-path
[`trace-import-from-derivation`]: @docroot@/command-ref/conf-file.md#conf-trace-import-from-derivation
[`allow-import-from-derivation`]: @docroot@/command-ref/conf-file.md#conf-allow-import-from-derivation
[`builtins.sort`]: @docroot@/language/builtins.md#builtins-sort
[`flake = false`]: @docroot@/command-ref/new-cli/nix3-flake.md?highlight=false#flake-inputs
[`--no-check-sigs`]: @docroot@/command-ref/new-cli/nix3-flake-archive.md#opt-no-check-sigs
[commit `299141e`]: https://github.com/NixOS/nix/commit/299141ecbd08bae17013226dbeae71e842b4fdd7
