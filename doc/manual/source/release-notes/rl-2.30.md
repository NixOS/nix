# Release 2.30.0 (2025-07-07)

- `build-dir` no longer defaults to `$TMPDIR`

  The directory in which temporary build directories are created no longer defaults
  to `TMPDIR` or `/tmp`, to avoid builders making their directories
  world-accessible. This behavior allowed escaping the build sandbox and can
  cause build impurities even when not used maliciously. We now default to `builds`
  in `NIX_STATE_DIR` (which is `/nix/var/nix/builds` in the default configuration).

- Deprecate manually making structured attrs with `__json = ...;` [#13220](https://github.com/NixOS/nix/pull/13220)

  The proper way to create a derivation using [structured attrs] in the Nix language is by using `__structuredAttrs = true` with [`builtins.derivation`].
  However, by exploiting how structured attrs are implementated, it has also been possible to create them by setting the `__json` environment variable to a serialized JSON string.
  This sneaky alternative method is now deprecated, and may be disallowed in future versions of Nix.

  [structured attrs]: @docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs
  [`builtins.derivation`]: @docroot@/language/builtins.html#builtins-derivation

- Add stack sampling evaluation profiler [#13220](https://github.com/NixOS/nix/pull/13220)

  Nix evaluator now supports stack sampling evaluation profiling via `--eval-profiler flamegraph` setting.
  It collects collapsed call stack information to output file specified by
  `--eval-profile-file` (`nix.profile` by default) in a format directly consumable
  by `flamegraph.pl` and compatible tools like [speedscope](https://speedscope.app/).
  Sampling frequency can be configured via `--eval-profiler-frequency` (99 Hz by default).

  Unlike existing `--trace-function-calls` this profiler includes the name of the function
  being called when it's available.

- `json-log-path` setting [#13003](https://github.com/NixOS/nix/pull/13003)

  New setting `json-log-path` that sends a copy of all Nix log messages (in JSON format) to a file or Unix domain socket.

- Rename `nix profile install` to `nix profile add` [#13224](https://github.com/NixOS/nix/pull/13224)

  The command `nix profile install` has been renamed to `nix profile add` (though the former is still available as an alias). This is because the verb "add" is a better antonym for the verb "remove" (i.e. `nix profile remove`). Nix also does not have install hooks or general behavior often associated with "installing".

- Non-flake inputs now contain a `sourceInfo` attribute [#13164](https://github.com/NixOS/nix/issues/13164) [#13170](https://github.com/NixOS/nix/pull/13170)

  Flakes have always a `sourceInfo` attribute which describes the source of the flake.
  The `sourceInfo.outPath` is often identical to the flake's `outPath`, however it can differ when the flake is located in a subdirectory of its source.

  Non-flake inputs (i.e. inputs with `flake = false`) can also be located at some path _within_ a wider source.
  This usually happens when defining a relative path input within the same source as the parent flake, e.g. `inputs.foo.url = ./some-file.nix`.
  Such relative inputs will now inherit their parent's `sourceInfo`.

  This also means it is now possible to use `?dir=subdir` on non-flake inputs.

  This iterates on the work done in 2.26 to improve relative path support ([#10089](https://github.com/NixOS/nix/pull/10089)),
  and resolves a regression introduced in 2.28 relating to nested relative path inputs ([#13164](https://github.com/NixOS/nix/issues/13164)).

- Revert incomplete closure mixed download and build feature [#77](https://github.com/NixOS/nix/issues/77) [#12628](https://github.com/NixOS/nix/issues/12628) [#13176](https://github.com/NixOS/nix/pull/13176)

  Since Nix 1.3 (299141ecbd08bae17013226dbeae71e842b4fdd7 in 2013) Nix has attempted to mix together upstream fresh builds and downstream substitutions when remote substuters contain an "incomplete closure" (have some store objects, but not the store objects they reference).
  This feature is now removed.

  Worst case, removing this feature could cause more building downstream, but it should not cause outright failures, since this is not happening for opaque store objects that we don't know how to build if we decide not to substitute.
  In practice, however, we doubt even the more building is very likely to happen.
  Remote stores that are missing dependencies in arbitrary ways (e.g. corruption) don't seem to be very common.

  On the contrary, when remote stores fail to implement the [closure property](@docroot@/store/store-object.md#closure-property), it is usually an *intentional* choice on the part of the remote store, because it wishes to serve as an "overlay" store over another store, such as `https://cache.nixos.org`.
  If an "incomplete closure" is encountered in that situation, the right fix is not to do some sort of "franken-building" as this feature implemented, but instead to make sure both substituters are enabled in the settings.

  (In the future, we should make it easier for remote stores to indicate this to clients, to catch settings that won't work in general before a missing dependency is actually encountered.)


# Contributors


This release was made possible by the following 32 contributors:

- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Egor Konovalov [**(@egorkonovalov)**](https://github.com/egorkonovalov)
- PopeRigby [**(@poperigby)**](https://github.com/poperigby)
- Peder Bergebakken Sundt [**(@pbsds)**](https://github.com/pbsds)
- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- Gwenn Le Bihan [**(@gwennlbh)**](https://github.com/gwennlbh)
- Jade Masker [**(@donottellmetonottellyou)**](https://github.com/donottellmetonottellyou)
- Nikita Krasnov [**(@synalice)**](https://github.com/synalice)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Wolfgang Walther [**(@wolfgangwalther)**](https://github.com/wolfgangwalther)
- Samuli Thomasson [**(@SimSaladin)**](https://github.com/SimSaladin)
- h0nIg [**(@h0nIg)**](https://github.com/h0nIg)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- Vladimír Čunát [**(@vcunat)**](https://github.com/vcunat)
- Graham Christensen [**(@grahamc)**](https://github.com/grahamc)
- kstrafe [**(@kstrafe)**](https://github.com/kstrafe)
- gustavderdrache [**(@gustavderdrache)**](https://github.com/gustavderdrache)
- Matt Sturgeon [**(@MattSturgeon)**](https://github.com/MattSturgeon)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Tristan Ross [**(@RossComputerGuy)**](https://github.com/RossComputerGuy)
- jayeshv [**(@jayeshv)**](https://github.com/jayeshv)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- pennae [**(@pennae)**](https://github.com/pennae)
- Luc Perkins [**(@lucperkins)**](https://github.com/lucperkins)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Pol Dellaiera [**(@drupol)**](https://github.com/drupol)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Seth Flynn [**(@getchoo)**](https://github.com/getchoo)
- Jonas Chevalier [**(@zimbatm)**](https://github.com/zimbatm)
- Stefan Boca [**(@stefanboca)**](https://github.com/stefanboca)
- Jeremy Fleischman [**(@jfly)**](https://github.com/jfly)
- Philipp Otterbein
- Raito Bezarius
