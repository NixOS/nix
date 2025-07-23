# Release 2.29.0 (2025-05-14)

After the special backport-based release of Nix 2.28 (timed to coincide with Nixpkgs 25.05), the release process is back to normal with 2.29.
As such, we have slightly more weeks of work from `master` (since 2.28 was branched from 2.27) than usual.
This fact is counterbalanced by the fact that most of those changes are bug fixes rather than larger new features.

- Prettified JSON output on the terminal [#12555](https://github.com/NixOS/nix/issues/12555) [#12652](https://github.com/NixOS/nix/pull/12652)

  This makes the output easier to read.

  Scripts are mostly unaffected because for those, stdout will be a file or a pipe, not a terminal, and for those, the old single-line behavior applies.

  `--json --pretty` can be passed to enable it even if the output is not a terminal.
  If your script creates a pseudoterminal for Nix's stdout, you can pass `--no-pretty` to disable the new behavior.

- Repl: improve continuation prompt for incomplete expressions [#12846](https://github.com/NixOS/nix/pull/12846)

  Improved REPL user experience by updating the continuation prompt from invisible blank spaces to a visible `" > "`, enhancing clarity when entering multi-line expressions.

- REPL `:load-flake` and `:reload` now work together [#8753](https://github.com/NixOS/nix/issues/8753) [#13180](https://github.com/NixOS/nix/pull/13180)

  Previously, `:reload` only reloaded the files specified with `:load` (or on the command line).
  Now, it also works with the flakes specified with `:load-flake` (or on the command line).
  This makes it correctly reload everything that was previously loaded, regardless of what sort of thing (plain file or flake) each item is.

- Increase retry delays on HTTP 429 Too Many Requests [#13052](https://github.com/NixOS/nix/pull/13052)

  When downloading Nix, the retry delay was previously set to 0.25 seconds. It has now been increased to 1 minute to better handle transient CI errors, particularly on GitHub.

- S3: opt-in the STSProfileCredentialsProvider [#12646](https://github.com/NixOS/nix/pull/12646)

  Added support for STS-based authentication for S3-based binary caches, i.e. enabling seamless integration with `aws sso login`.

- Reduce connect timeout for http substituter [#12876](https://github.com/NixOS/nix/pull/12876)

  Previously, the Nix setting `connect-timeout` had no limit. It is now set to `5s`, offering a more practical default for users self-hosting binary caches, which may occasionally become unavailable, such as during updates.


- C API: functions for locking and loading a flake [#10435](https://github.com/NixOS/nix/issues/10435) [#12877](https://github.com/NixOS/nix/pull/12877) [#13098](https://github.com/NixOS/nix/pull/13098)

  This release adds functions to the C API for handling the loading of flakes. Previously, this had to be worked around by using `builtins.getFlake`.
  C API consumers and language bindings now have access to basic locking functionality.

  It does not expose the full locking API, so that the implementation can evolve more freely.
  Locking is controlled with the functions, which cover the common use cases for consuming a flake:
  - `nix_flake_lock_flags_set_mode_check`
  - `nix_flake_lock_flags_set_mode_virtual`
  - `nix_flake_lock_flags_set_mode_write_as_needed`
  - `nix_flake_lock_flags_add_input_override`, which also enables `virtual`

  This change also introduces the new `nix-fetchers-c` library, whose single purpose for now is to manage the (`nix.conf`) settings for the built-in fetchers.

  More details can be found in the [C API documentation](@docroot@/c-api.md).

- No longer copy flakes that are in the nix store [#10435](https://github.com/NixOS/nix/issues/10435) [#12877](https://github.com/NixOS/nix/pull/12877) [#13098](https://github.com/NixOS/nix/pull/13098)

  Previously, we would duplicate entries like `path:/nix/store/*` back into the Nix store.
  This was prominently visible for pinned system flake registry entries in NixOS, e.g., when running `nix run nixpkgs#hello`.

- Consistently preserve error messages from cached evaluation [#12762](https://github.com/NixOS/nix/issues/12762) [#12809](https://github.com/NixOS/nix/pull/12809)

  In one code path, we are not returning the errors cached from prior evaluation, but instead throwing generic errors stemming from the lack of value (due to the error).
  These generic error messages were far less informative.
  Now we consistently return the original error message.

- Faster blake3 hashing [#12676](https://github.com/NixOS/nix/pull/12676)

  The implementation for blake3 hashing is now multi-threaded and used memory-mapped IO.
  Benchmark results can be found the [pull request](https://github.com/NixOS/nix/pull/12676).

- Fix progress bar for S3 binary caches and make file transfers interruptible [#12877](https://github.com/NixOS/nix/issues/12877) [#13098](https://github.com/NixOS/nix/issues/13098) [#12538](https://github.com/NixOS/nix/pull/12538)

  The progress bar now correctly display upload/download progress for S3 up/downloads. S3 uploads are now interruptible.

- Add host attribute of github/gitlab flakerefs to URL serialization [#12580](https://github.com/NixOS/nix/pull/12580)

  Resolved an issue where `github:` or `gitlab:` URLs lost their `host` attribute when written to a lockfile, resulting in invalid URLs.

- Multiple signatures support in store urls [#12976](https://github.com/NixOS/nix/pull/12976)

  Added support for a `secretKeyFiles` URI parameter in Nix store URIs, allowing multiple signing key files to be specified as a comma-separated list.
  This enables signing paths with multiple keys. This helps with [RFC #149](https://github.com/NixOS/rfcs/pull/149) to enable binary cache key rotation in the NixOS infra.

  Example usage:

  ```bash
  nix copy --to "file:///tmp/store?secret-keys=/tmp/key1,/tmp/key2" \
    "$(nix build --print-out-paths nixpkgs#hello)"
  ```

- nix flake show now skips over import-from-derivation [#4265](https://github.com/NixOS/nix/issues/4265) [#12583](https://github.com/NixOS/nix/pull/12583)

  Previously, if a flake contained outputs relying on [import from derivation](@docroot@/language/import-from-derivation.md) during evaluation, `nix flake show` would fail to display the rest of the flake. The updated behavior skips such outputs, allowing the rest of the flake to be shown.

- Add `nix formatter build` and `nix formatter run` commands [#13063](https://github.com/NixOS/nix/pull/13063)

  `nix formatter run` is an alias for `nix fmt`. Nothing new there.

  `nix formatter build` is sort of like `nix build`: it builds, links, and prints a path to the formatter program:

  ```
  $ nix formatter build
  /nix/store/cb9w44vkhk2x4adfxwgdkkf5gjmm856j-treefmt/bin/treefmt
  ```

  Note that unlike `nix build`, this prints the full path to the program, not just the store path (in the example above that would be `/nix/store/cb9w44vkhk2x4adfxwgdkkf5gjmm856j-treefmt`).

- Amend OSC 8 escape stripping for xterm-style separator [#13109](https://github.com/NixOS/nix/pull/13109)

  Improve terminal escape code filtering to understand a second type of hyperlink escape codes.
  This in particular prevents parts of GCC 14's diagnostics from being improperly filtered away.


## Contributors


This release was made possible by the following 40 contributors:

- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- The Tumultuous Unicorn Of Darkness [**(@TheTumultuousUnicornOfDarkness)**](https://github.com/TheTumultuousUnicornOfDarkness)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Félix [**(@picnoir)**](https://github.com/picnoir)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Vincent Breitmoser [**(@Valodim)**](https://github.com/Valodim)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- ulucs [**(@ulucs)**](https://github.com/ulucs)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Andrey Butirsky [**(@bam80)**](https://github.com/bam80)
- Dean De Leo [**(@whatsthecraic)**](https://github.com/whatsthecraic)
- Las Safin [**(@L-as)**](https://github.com/L-as)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Shahar "Dawn" Or [**(@mightyiam)**](https://github.com/mightyiam)
- Ryan Hendrickson [**(@rhendric)**](https://github.com/rhendric)
- Rodney Lorrimar [**(@rvl)**](https://github.com/rvl)
- Erik Nygren [**(@Kirens)**](https://github.com/Kirens)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Martin Fischer [**(@not-my-profile)**](https://github.com/not-my-profile)
- Graham Christensen [**(@grahamc)**](https://github.com/grahamc)
- Vit Gottwald [**(@VitGottwald)**](https://github.com/VitGottwald)
- silvanshade [**(@silvanshade)**](https://github.com/silvanshade)
- Illia Bobyr [**(@ilya-bobyr)**](https://github.com/ilya-bobyr)
- Jeremy Fleischman [**(@jfly)**](https://github.com/jfly)
- Ruby Rose [**(@oldshensheep)**](https://github.com/oldshensheep)
- Sergei Trofimovich [**(@trofi)**](https://github.com/trofi)
- Tim [**(@Jaculabilis)**](https://github.com/Jaculabilis)
- Anthony Wang [**(@anthowan)**](https://github.com/anthowan)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Sandro [**(@SuperSandro2000)**](https://github.com/SuperSandro2000)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Dmitry Bogatov [**(@KAction)**](https://github.com/KAction)
- Sizhe Zhao [**(@Prince213)**](https://github.com/Prince213)
- jade [**(@lf-)**](https://github.com/lf-)
- Pierre-Etienne Meunier [**(@P-E-Meunier)**](https://github.com/P-E-Meunier)
- Alexander Romanov [**(@ajlekcahdp4)**](https://github.com/ajlekcahdp4)
- Domagoj Mišković [**(@allrealmsoflife)**](https://github.com/allrealmsoflife)
- Thomas Miedema [**(@thomie)**](https://github.com/thomie)
- Yannik Sander [**(@ysndr)**](https://github.com/ysndr)
- Philipp Otterbein
- Dmitry Bogatov
