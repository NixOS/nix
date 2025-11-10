# Release 2.24.0 (2024-07-31)

### Significant changes

- Harden user sandboxing

  The build directory has been hardened against interference with the outside world by nesting it inside another directory owned by (and only readable by) the daemon user.

  This is a low severity security fix, [CVE-2024-38531](https://www.cve.org/CVERecord?id=CVE-2024-38531).

  Credit: [**@alois31**](https://github.com/alois31), [**Linus Heckemann (@lheckemann)**](https://github.com/lheckemann)
  Co-authors: [**@edolstra**](https://github.com/edolstra)

- `nix-shell <directory>` looks for `shell.nix` [#496](https://github.com/NixOS/nix/issues/496) [#2279](https://github.com/NixOS/nix/issues/2279) [#4529](https://github.com/NixOS/nix/issues/4529) [#5431](https://github.com/NixOS/nix/issues/5431) [#11053](https://github.com/NixOS/nix/issues/11053) [#11057](https://github.com/NixOS/nix/pull/11057)

  `nix-shell $x` now looks for `$x/shell.nix` when `$x` resolves to a directory.

  Although this might be seen as a breaking change, its primarily interactive usage makes it a minor issue.
  This adjustment addresses a commonly reported problem.

  This also applies to `nix-shell` shebang scripts. Consider the following example:

  ```shell
  #!/usr/bin/env nix-shell
  #!nix-shell -i bash
  ```

  This will now load `shell.nix` from the script's directory, if it exists; `default.nix` otherwise.

  The old behavior can be opted into by setting the option [`nix-shell-always-looks-for-shell-nix`](@docroot@/command-ref/conf-file.md#conf-nix-shell-always-looks-for-shell-nix) to `false`.

  Author: [**Robert Hensing (@roberth)**](https://github.com/roberth)

- `nix-repl`'s `:doc` shows documentation comments [#3904](https://github.com/NixOS/nix/issues/3904) [#10771](https://github.com/NixOS/nix/issues/10771) [#1652](https://github.com/NixOS/nix/pull/1652) [#9054](https://github.com/NixOS/nix/pull/9054) [#11072](https://github.com/NixOS/nix/pull/11072)

  `nix repl` has a `:doc` command that previously only rendered documentation for internally defined functions.
  This feature has been extended to also render function documentation comments, in accordance with [RFC 145].

  Example:

  ```
  nix-repl> :doc lib.toFunction
  Function toFunction
      … defined at /home/user/h/nixpkgs/lib/trivial.nix:1072:5

      Turns any non-callable values into constant functions. Returns
      callable values as is.

  Inputs

      v

        : Any value

  Examples

      :::{.example}

  ## lib.trivial.toFunction usage example

        | nix-repl> lib.toFunction 1 2
        | 1
        |
        | nix-repl> lib.toFunction (x: x + 1) 2
        | 3

      :::
  ```

  Known limitations:
  - It does not render documentation for "formals", such as `{ /** the value to return */ x, ... }: x`.
  - Some extensions to markdown are not yet supported, as you can see in the example above.

  We'd like to acknowledge [Yingchi Long (@inclyc)](https://github.com/inclyc) for proposing a proof of concept for this functionality in [#9054](https://github.com/NixOS/nix/pull/9054), as well as [@sternenseemann](https://github.com/sternenseemann) and [Johannes Kirschbauer (@hsjobeki)](https://github.com/hsjobeki) for their contributions, proposals, and their work on [RFC 145].

  Author: [**Robert Hensing (@roberth)**](https://github.com/roberth)

  [RFC 145]: https://github.com/NixOS/rfcs/pull/145

### Other changes

- Solve `cached failure of attribute X` [#9165](https://github.com/NixOS/nix/issues/9165) [#10513](https://github.com/NixOS/nix/issues/10513) [#10564](https://github.com/NixOS/nix/pull/10564)

  This eliminates all "cached failure of attribute X" messages by forcing evaluation of the original value when needed to show the exception to the user. This enhancement improves error reporting by providing the underlying message and stack trace.

  Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)

- Run the flake regressions test suite [#10603](https://github.com/NixOS/nix/pull/10603)

  This update introduces a GitHub action to run a subset of the [flake regressions test suite](https://github.com/NixOS/flake-regressions), which includes 259 flakes with their expected evaluation results. Currently, the action runs the first 25 flakes due to the full test suite's extensive runtime. A manually triggered action may be implemented later to run the entire test suite.

  Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)

- Support unit prefixes in configuration settings [#10668](https://github.com/NixOS/nix/pull/10668)

  Configuration settings in Nix now support unit prefixes, allowing for more intuitive and readable configurations. For example, you can now specify [`--min-free 1G`](@docroot@/command-ref/conf-file.md#conf-min-free) to set the minimum free space to 1 gigabyte.

  This enhancement was extracted from [#7851](https://github.com/NixOS/nix/pull/7851) and is also useful for PR [#10661](https://github.com/NixOS/nix/pull/10661).

  Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)

- `nix build`: show all FOD errors with `--keep-going` [#10734](https://github.com/NixOS/nix/pull/10734)

  The [`nix build`](@docroot@/command-ref/new-cli/nix3-build.md) command has been updated to improve the behavior of the [`--keep-going`] flag. Now, when `--keep-going` is used, all hash-mismatch errors of failing fixed-output derivations (FODs) are displayed, similar to the behavior for other build failures. This enhancement ensures that all relevant build errors are shown, making it easier for users to update multiple derivations at once or to diagnose and fix issues.

  Author: [**Jörg Thalheim (@Mic92)**](https://github.com/Mic92), [**Maximilian Bosch (@Ma27)**](https://github.com/Ma27)

  [`--keep-going`](@docroot@/command-ref/opt-common.md#opt-keep-going)

- Build with Meson [#2503](https://github.com/NixOS/nix/issues/2503) [#10378](https://github.com/NixOS/nix/pull/10378) [#10855](https://github.com/NixOS/nix/pull/10855) [#10904](https://github.com/NixOS/nix/pull/10904) [#10908](https://github.com/NixOS/nix/pull/10908) [#10914](https://github.com/NixOS/nix/pull/10914) [#10933](https://github.com/NixOS/nix/pull/10933) [#10936](https://github.com/NixOS/nix/pull/10936) [#10954](https://github.com/NixOS/nix/pull/10954) [#10955](https://github.com/NixOS/nix/pull/10955) [#10963](https://github.com/NixOS/nix/pull/10963) [#10967](https://github.com/NixOS/nix/pull/10967) [#10973](https://github.com/NixOS/nix/pull/10973) [#11034](https://github.com/NixOS/nix/pull/11034) [#11054](https://github.com/NixOS/nix/pull/11054) [#11055](https://github.com/NixOS/nix/pull/11055) [#11060](https://github.com/NixOS/nix/pull/11060) [#11064](https://github.com/NixOS/nix/pull/11064) [#11155](https://github.com/NixOS/nix/pull/11155)

  These changes aim to replace the use of autotools and `make` with Meson for building various components of Nix. Additionally, each library is built in its own derivation, leveraging Meson's "subprojects" feature to allow a single development shell for building all libraries while also supporting separate builds. This approach aims to improve productivity and build modularity, compared to both make and a monolithic Meson-based derivation.

  Special thanks to everyone who has contributed to the Meson port, particularly [**@p01arst0rm**](https://github.com/p01arst0rm) and [**@Qyriad**](https://github.com/Qyriad).

  Authors: [**John Ericson (@Ericson2314)**](https://github.com/Ericson2314), [**Tom Bereknyei**](https://github.com/tomberek), [**Théophane Hufschmitt (@thufschmitt)**](https://github.com/thufschmitt), [**Valentin Gagarin (@fricklerhandwerk)**](https://github.com/fricklerhandwerk), [**Robert Hensing (@roberth)**](https://github.com/roberth)
  Co-authors: [**@p01arst0rm**](https://github.com/p01arst0rm), [**@Qyriad**](https://github.com/Qyriad)

- Evaluation cache: fix cache regressions [#10570](https://github.com/NixOS/nix/issues/10570) [#11086](https://github.com/NixOS/nix/pull/11086)

  This update addresses two bugs in the evaluation cache system:

  1. Regression in #10570: The evaluation cache was not being persisted in `nix develop`.
  2. Nix could sometimes try to commit the evaluation cache SQLite transaction without there being an active transaction, resulting in non-error errors being printed.

  Author: [**Lexi Mattick (@kognise)**](https://github.com/kognise)

- Introduce `libnixflake` [#9063](https://github.com/NixOS/nix/pull/9063)

  A new library, `libnixflake`, has been introduced to better separate the Flakes layer within Nix. This change refactors the codebase to encapsulate Flakes-specific functionality within its own library.

  See the commits in the pull request for detailed changes, with the only significant code modifications happening in the initial commit.

  This change was alluded to in [RFC 134](https://github.com/nixos/rfcs/blob/master/rfcs/0134-nix-store-layer.md) and is a step towards a more modular and maintainable codebase.

  Author: [**John Ericson (@Ericson2314)**](https://github.com/Ericson2314)

- CLI options `--arg-from-file` and `--arg-from-stdin` [#9913](https://github.com/NixOS/nix/pull/9913)

- The `--debugger` now prints source location information, instead of the
  pointers of source location information. Before:

  ```
  nix-repl> :bt
  0: while evaluating the attribute 'python311.pythonForBuild.pkgs'
  0x600001522598
  ```

  After:

  ```
  0: while evaluating the attribute 'python311.pythonForBuild.pkgs'
  /nix/store/hg65h51xnp74ikahns9hyf3py5mlbbqq-source/overrides/default.nix:132:27

     131|
     132|       bootstrappingBase = pkgs.${self.python.pythonAttr}.pythonForBuild.pkgs;
        |                           ^
     133|     in
  ```

- Stop vendoring `toml11`

  We don't apply any patches to it, and vendoring it locks users into
  bugs (it hasn't been updated since its introduction in late 2021).

  Author: [**Winter (@winterqt)**](https://github.com/winterqt)

- Rename hash format `base32` to `nix32` [#8678](https://github.com/NixOS/nix/pull/8678)

  Hash format `base32` was renamed to `nix32` since it used a special nix-specific character set for
  [Base32](https://en.wikipedia.org/wiki/Base32).

  **Deprecation**: Use `nix32` instead of `base32` as `toHashFormat`

  For the builtin `convertHash`, the `toHashFormat` parameter now accepts the same hash formats as the `--to`/`--from`
  parameters of the `nix hash convert` command: `"base16"`, `"nix32"`, `"base64"`, and `"sri"`. The former `"base32"` value
  remains as a deprecated alias for `"nix32"`. Please convert your code from:

  ```nix
  builtins.convertHash { inherit hash hashAlgo; toHashFormat = "base32";}
  ```

  to

  ```nix
  builtins.convertHash { inherit hash hashAlgo; toHashFormat = "nix32";}
  ```

- Add `pipe-operators` experimental feature [#11131](https://github.com/NixOS/nix/pull/11131)

  This is a draft implementation of [RFC 0148](https://github.com/NixOS/rfcs/pull/148).

  The `pipe-operators` experimental feature adds [`<|` and `|>` operators][pipe operators] to the Nix language.
  *a* `|>` *b* is equivalent to the function application *b* *a*, and
  *a* `<|` *b* is equivalent to the function application *a* *b*.

  For example:

  ```
  nix-repl> 1 |> builtins.add 2 |> builtins.mul 3
  9

  nix-repl> builtins.add 1 <| builtins.mul 2 <| 3
  7
  ```

  `<|` and `|>` are right and left associative, respectively, and have lower precedence than any other operator.
  These properties may change in future releases.

  See [the RFC](https://github.com/NixOS/rfcs/pull/148) for more examples and rationale.

  [pipe operators]: @docroot@/language/operators.md#pipe-operators

- `nix-shell` shebang uses relative path [#4232](https://github.com/NixOS/nix/issues/4232) [#5088](https://github.com/NixOS/nix/pull/5088) [#11058](https://github.com/NixOS/nix/pull/11058)

  <!-- unfortunately no link target for the specific syntax -->
  Relative [path](@docroot@/language/types.md#type-path) literals in `nix-shell` shebang scripts' options are now resolved relative to the [script's location](@docroot@/glossary.md?highlight=base%20directory#gloss-base-directory).
  Previously they were resolved relative to the current working directory.

  For example, consider the following script in `~/myproject/say-hi`:

  ```shell
  #!/usr/bin/env nix-shell
  #!nix-shell --expr 'import ./shell.nix'
  #!nix-shell --arg toolset './greeting-tools.nix'
  #!nix-shell -i bash
  hello
  ```

  Older versions of `nix-shell` would resolve `shell.nix` relative to the current working directory, such as the user's home directory in this example:

  ```console
  [hostname:~]$ ./myproject/say-hi
  error:
         … while calling the 'import' builtin
           at «string»:1:2:
              1| (import ./shell.nix)
               |  ^

         error: path '/home/user/shell.nix' does not exist
  ```

  Since this release, `nix-shell` resolves `shell.nix` relative to the script's location, and `~/myproject/shell.nix` is used.

  ```console
  $ ./myproject/say-hi
  Hello, world!
  ```

  **Opt-out**

  This is technically a breaking change, so we have added an option so you can adapt independently of your Nix update.
  The old behavior can be opted into by setting the option [`nix-shell-shebang-arguments-relative-to-script`](@docroot@/command-ref/conf-file.md#conf-nix-shell-shebang-arguments-relative-to-script) to `false`.
  This option will be removed in a future release.

  Author: [**Robert Hensing (@roberth)**](https://github.com/roberth)

- Improve handling of tarballs that don't consist of a single top-level directory [#11195](https://github.com/NixOS/nix/pull/11195)

  In previous Nix releases, the tarball fetcher (used by `builtins.fetchTarball`) erroneously merged top-level directories into a single directory, and silently discarded top-level files that are not directories. This is no longer the case. The new behaviour is that *only* if the tarball consists of a single directory, the top-level path component of the files in the tarball is removed (similar to `tar`'s `--strip-components=1`).

  Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)

- Setting to warn about large paths [#10778](https://github.com/NixOS/nix/pull/10778)

  Nix can now warn when evaluation of a Nix expression causes a large
  path to be copied to the Nix store. The threshold for this warning can
  be configured using the `warn-large-path-threshold` setting,
  e.g. `--warn-large-path-threshold 100M`.


## Contributors

This release was made possible by the following 43 contributors:

- Andreas Rammhold [**(@andir)**](https://github.com/andir)
- Andrew Marshall [**(@amarshall)**](https://github.com/amarshall)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- Cameron [**(@SkamDart)**](https://github.com/SkamDart)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Corbin Simpson [**(@MostAwesomeDude)**](https://github.com/MostAwesomeDude)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Emily [**(@emilazy)**](https://github.com/emilazy)
- Enno Richter [**(@elohmeier)**](https://github.com/elohmeier)
- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- HaeNoe [**(@haenoe)**](https://github.com/haenoe)
- Hamir Mahal [**(@hamirmahal)**](https://github.com/hamirmahal)
- Harmen [**(@alicebob)**](https://github.com/alicebob)
- Ivan Trubach [**(@tie)**](https://github.com/tie)
- Jared Baur [**(@jmbaur)**](https://github.com/jmbaur)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Jonathan De Troye [**(@detroyejr)**](https://github.com/detroyejr)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Klemens Nanni [**(@klemensn)**](https://github.com/klemensn)
- Las Safin [**(@L-as)**](https://github.com/L-as)
- Lexi Mattick [**(@kognise)**](https://github.com/kognise)
- Matthew Bauer [**(@matthewbauer)**](https://github.com/matthewbauer)
- Max “Goldstein” Siling [**(@GoldsteinE)**](https://github.com/GoldsteinE)
- Mingye Wang [**(@Artoria2e5)**](https://github.com/Artoria2e5)
- Philip Taron [**(@philiptaron)**](https://github.com/philiptaron)
- Pierre Bourdon [**(@delroth)**](https://github.com/delroth)
- Pino Toscano [**(@pinotree)**](https://github.com/pinotree)
- RTUnreal [**(@RTUnreal)**](https://github.com/RTUnreal)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Romain Neil [**(@romain-neil)**](https://github.com/romain-neil)
- Ryan Hendrickson [**(@rhendric)**](https://github.com/rhendric)
- Sergei Trofimovich [**(@trofi)**](https://github.com/trofi)
- Shogo Takata [**(@pineapplehunter)**](https://github.com/pineapplehunter)
- Siddhant Kumar [**(@siddhantk232)**](https://github.com/siddhantk232)
- Silvan Mosberger [**(@infinisil)**](https://github.com/infinisil)
- Théophane Hufschmitt [**(@thufschmitt)**](https://github.com/thufschmitt)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- Winter [**(@winterqt)**](https://github.com/winterqt)
- jade [**(@lf-)**](https://github.com/lf-)
- kirillrdy [**(@kirillrdy)**](https://github.com/kirillrdy)
- pennae [**(@pennae)**](https://github.com/pennae)
- poweredbypie [**(@poweredbypie)**](https://github.com/poweredbypie)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
