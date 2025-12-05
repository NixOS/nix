# Release 2.23.0 (2024-06-03)

- New builtin: `builtins.warn` [#306026](https://github.com/NixOS/nix/issues/306026) [#10592](https://github.com/NixOS/nix/pull/10592)

  `builtins.warn` behaves like `builtins.trace "warning: ${msg}"`, has an accurate log level, and is controlled by the options
  [`debugger-on-trace`](@docroot@/command-ref/conf-file.md#conf-debugger-on-trace),
  [`debugger-on-warn`](@docroot@/command-ref/conf-file.md#conf-debugger-on-warn) and
  [`abort-on-warn`](@docroot@/command-ref/conf-file.md#conf-abort-on-warn).

- Make `nix build --keep-going` consistent with `nix-build --keep-going`

  This means that if e.g. multiple fixed-output derivations fail to
  build, all hash mismatches are displayed.

- Modify `nix derivation {add,show}` JSON format [#9866](https://github.com/NixOS/nix/issues/9866) [#10722](https://github.com/NixOS/nix/pull/10722)

  The JSON format for derivations has been slightly revised to better conform to our [JSON guidelines](@docroot@/development/json-guideline.md).
  In particular, the hash algorithm and content addressing method of content-addressed derivation outputs are now separated into two fields `hashAlgo` and `method`,
  rather than one field with an arcane `:`-separated format.

  This JSON format is only used by the experimental `nix derivation` family of commands, at this time.
  Future revisions are expected as the JSON format is still not entirely in compliance even after these changes.

- Warn on unknown settings anywhere in the command line [#10701](https://github.com/NixOS/nix/pull/10701)

  All `nix` commands will now properly warn when an unknown option is specified anywhere in the command line.

  Before:

  ```console
  $ nix-instantiate --option foobar baz --expr '{}'
  warning: unknown setting 'foobar'
  $ nix-instantiate '{}' --option foobar baz --expr
  $ nix eval --expr '{}' --option foobar baz
  { }
  ```

  After:

  ```console
  $ nix-instantiate --option foobar baz --expr '{}'
  warning: unknown setting 'foobar'
  $ nix-instantiate '{}' --option foobar baz --expr
  warning: unknown setting 'foobar'
  $ nix eval --expr '{}' --option foobar baz
  warning: unknown setting 'foobar'
  { }
  ```

- `nix env shell` is the new `nix shell`, and `nix shell` remains an accepted alias [#10504](https://github.com/NixOS/nix/issues/10504) [#10807](https://github.com/NixOS/nix/pull/10807)

  This is part of an effort to bring more structure to the CLI subcommands.

  `nix env` will be about the process environment.
  Future commands may include `nix env run` and `nix env print-env`.

  It is also somewhat analogous to the [planned](https://github.com/NixOS/nix/issues/10504) `nix dev shell` (currently `nix develop`), which is less about environment variables, and more about running a development shell, which is a more powerful command, but also requires more setup.

- Flake operations that expect derivations now print the failing value and its type [#10778](https://github.com/NixOS/nix/pull/10778)

  In errors like `flake output attribute 'nixosConfigurations.yuki.config' is not a derivation or path`, the message now includes the failing value and type.

  Before:

  ```
  error: flake output attribute 'nixosConfigurations.yuki.config' is not a derivation or path
  ````

  After:

  ```
  error: expected flake output attribute 'nixosConfigurations.yuki.config' to be a derivation or path but found a set: { appstream = «thunk»; assertions = «thunk»; boot = { bcache = «thunk»; binfmt = «thunk»; binfmtMiscRegistrations = «thunk»; blacklistedKernelModules = «thunk»; bootMount = «thunk»; bootspec = «thunk»; cleanTmpDir = «thunk»; consoleLogLevel = «thunk»; «43 attributes elided» }; «48 attributes elided» }
  ```

- `fetchTree` now fetches Git repositories shallowly by default [#10028](https://github.com/NixOS/nix/pull/10028)

  `builtins.fetchTree` now clones Git repositories shallowly by default, which reduces network traffic and disk usage significantly in many cases.

  Previously, the default behavior was to clone the full history of a specific tag or branch (e.g. `ref`) and only afterwards extract the files of one specific revision.

  From now on, the `ref` and `allRefs` arguments will be ignored, except if shallow cloning is disabled by setting `shallow = false`.

  The defaults for `builtins.fetchGit` remain unchanged. Here, shallow cloning has to be enabled manually by passing `shallow = true`.

- Store object info JSON format now uses `null` rather than omitting fields [#9995](https://github.com/NixOS/nix/pull/9995)

  The [store object info JSON format](@docroot@/protocols/json/store-object-info.md), used for e.g. `nix path-info`, no longer omits fields to indicate absent information, but instead includes the fields with a `null` value.
  For example, `"ca": null` is used to indicate a store object that isn't content-addressed rather than omitting the `ca` field entirely.
  This makes records of this sort more self-describing, and easier to consume programmatically.

  We will follow this design principle going forward;
  the [JSON guidelines](@docroot@/development/json-guideline.md) in the contributing section have been updated accordingly.

- Large path warnings [#10661](https://github.com/NixOS/nix/pull/10661)

  Nix can now warn when evaluation of a Nix expression causes a large
  path to be copied to the Nix store. The threshold for this warning can
  be configured using [the `warn-large-path-threshold`
  setting](@docroot@/command-ref/conf-file.md#conf-warn-large-path-threshold),
  e.g. `--warn-large-path-threshold 100M` will warn about paths larger
  than 100 MiB.

