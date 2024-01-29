# Release 2.20.0 (2024-01-29)

- Option `allowed-uris` can now match whole schemes in URIs without slashes [#9547](https://github.com/NixOS/nix/pull/9547)

  If a scheme, such as `github:` is specified in the `allowed-uris` option, all URIs starting with `github:` are allowed.
  Previously this only worked for schemes whose URIs used the `://` syntax.

- Make `nix store gc` use the auto-GC policy [#7851](https://github.com/NixOS/nix/pull/7851)



- Include cgroup stats when building through the daemon [#9598](https://github.com/NixOS/nix/pull/9598)

  Nix now also reports cgroup statistics when building through the nix daemon and when doing remote builds using ssh-ng,
  if both sides of the connection are this version of Nix or newer.

- Fix handling of truncated `.drv` files. [#9673](https://github.com/NixOS/nix/pull/9673)

  Previously a `.drv` that was truncated in the middle of a string would case nix to enter an infinite loop, eventually exhausting all memory and crashing.

- Disallow empty search regex in `nix search` [#9481](https://github.com/NixOS/nix/pull/9481)

  [`nix search`](@docroot@/command-ref/new-cli/nix3-search.md) now requires a search regex to be passed. To show all packages, use `^`.

- Reduce eval memory usage and wall time [#9658](https://github.com/NixOS/nix/pull/9658)

  Reduce the size of the `Env` struct used in the evaluator by a pointer, or 8 bytes on most modern machines.
  This reduces memory usage during eval by around 2% and wall time by around 3%.

- Add new `eval-system` setting [#4093](https://github.com/NixOS/nix/pull/4093)

  Add a new `eval-system` option.
  Unlike `system`, it just overrides the value of `builtins.currentSystem`.
  This is more useful than overriding `system`, because you can build these derivations on remote builders which can work on the given system.
  In contrast, `system` also effects scheduling which will cause Nix to build those derivations locally even if that doesn't make sense.

  `eval-system` only takes effect if it is non-empty.
  If empty (the default) `system` is used as before, so there is no breakage.

- Nix now uses `libgit2` for Git fetching [#5313](https://github.com/NixOS/nix/issues/5313) [#9240](https://github.com/NixOS/nix/pull/9240) [#9241](https://github.com/NixOS/nix/pull/9241) [#9258](https://github.com/NixOS/nix/pull/9258) [#9480](https://github.com/NixOS/nix/pull/9480)

  Nix has built-in support for fetching sources from Git, during evaluation and locking; outside the sandbox.
  The existing implementation based on the Git CLI had issues regarding reproducibility and performance.

  Most of the original `fetchGit` behavior has been implemented using the `libgit2` library, which gives the fetcher fine-grained control.

  Known issues:
  - The `export-subst` behavior has not been reimplemented. [Partial](https://github.com/NixOS/nix/pull/9391#issuecomment-1872503447) support for this Git feature is feasible, but it did not make the release window.

- Rename hash format `base32` to `nix32` [#9452](https://github.com/NixOS/nix/pull/9452)

  Hash format `base32` was renamed to `nix32` since it used a special nix-specific character set for
  [Base32](https://en.wikipedia.org/wiki/Base32).

  ## Deprecation: Use `nix32` instead of `base32` as `toHashFormat`

  For the builtin `convertHash`, the `toHashFormat` parameter now accepts the same hash formats as the `--to`/`--from`
  parameters of the `nix hash conert` command: `"base16"`, `"nix32"`, `"base64"`, and `"sri"`. The former `"base32"` value
  remains as a deprecated alias for `"base32"`. Please convert your code from:

  ```nix
  builtins.convertHash { inherit hash hashAlgo; toHashFormat = "base32";}
  ```

  to

  ```nix
  builtins.convertHash { inherit hash hashAlgo; toHashFormat = "nix32";}
  ```

- import-from-derivation builds the derivation in the build store [#9661](https://github.com/NixOS/nix/pull/9661)

  When using `--eval-store`, `import`ing from a derivation will now result in the derivation being built on the build store, i.e. the store specified in the `store` Nix option.

  Because the resulting Nix expression must be copied back to the eval store in order to be imported, this requires the eval store to trust the build store's signatures.

- Mounted SSH Store [#7890](https://github.com/NixOS/nix/issues/7890) [#7912](https://github.com/NixOS/nix/pull/7912)

  Introduced the store [`mounted-ssh-ng://`](@docroot@/command-ref/new-cli/nix3-help-stores.md).
  This store allows full access to a Nix store on a remote machine and additionally requires that the store be mounted in the local filesystem.

- Rename to `nix config show` [#7672](https://github.com/NixOS/nix/issues/7672) [#9477](https://github.com/NixOS/nix/pull/9477)

  `nix show-config` was renamed to `nix config show`, and `nix doctor` was renamed to `nix config check`, to be more consistent with the rest of the command-line interface.

- Fix `nix-env --query --drv-path --json` [#9257](https://github.com/NixOS/nix/pull/9257)

  Fixed a bug where `nix-env --query` ignored `--drv-path` when `--json` was set.

- Some stack overflow segfaults are fixed [#8882](https://github.com/NixOS/nix/issues/8882) [#8893](https://github.com/NixOS/nix/pull/8893)

  `nix flake check` now logs the checks it runs and the derivations it evaluates:

  ```
  $ nix flake check -v
  evaluating flake...
  checking flake output 'checks'...
  checking derivation 'checks.aarch64-darwin.ghciwatch-tests'...
  derivation evaluated to /nix/store/nh7dlvsrhds4cxl91mvgj4h5cbq6skmq-ghciwatch-test-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-clippy'...
  derivation evaluated to /nix/store/9cb5a6wmp6kf6hidqw9wphidvb8bshym-ghciwatch-clippy-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-doc'...
  derivation evaluated to /nix/store/8brdd3jbawfszpbs7vdpsrhy80as1il8-ghciwatch-doc-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-fmt'...
  derivation evaluated to /nix/store/wjhs0l1njl5pyji53xlmfjrlya0wmz8p-ghciwatch-fmt-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-audit'...
  derivation evaluated to /nix/store/z0mps8dyj2ds7c0fn0819y5h5611033z-ghciwatch-audit-0.3.0.drv
  checking flake output 'packages'...
  checking derivation 'packages.aarch64-darwin.default'...
  derivation evaluated to /nix/store/41abbdyglw5x9vcsvd89xan3ydjf8d7r-ghciwatch-0.3.0.drv
  checking flake output 'apps'...
  checking flake output 'devShells'...
  checking derivation 'devShells.aarch64-darwin.default'...
  derivation evaluated to /nix/store/bc935gz7dylzmcpdb5cczr8gngv8pmdb-nix-shell.drv
  running 5 flake checks...
  warning: The check omitted these incompatible systems: aarch64-linux, x86_64-darwin, x86_64-linux
  Use '--all-systems' to check all.
  ```

- Add `nix hash convert` [#9452](https://github.com/NixOS/nix/pull/9452)

  New [`nix hash convert`](https://github.com/NixOS/nix/issues/8876) sub command with a fast track
  to stabilization! Examples:

  - Convert the hash to `nix32`.

    ```bash
    $ nix hash convert --hash-algo "sha1" --to nix32 "800d59cfcd3c05e900cb4e214be48f6b886a08df"
    vw46m23bizj4n8afrc0fj19wrp7mj3c0
    ```
    `nix32` is a base32 encoding with a nix-specific character set.
    Explicitly specify the hashing algorithm (optional with SRI hashes) but detect hash format by the length of the input
    hash.
  - Convert the hash to the `sri` format that includes an algorithm specification:
    ```bash
    nix hash convert --hash-algo "sha1" "800d59cfcd3c05e900cb4e214be48f6b886a08df"
    sha1-gA1Zz808BekAy04hS+SPa4hqCN8=
    ```
    or with an explicit `-to` format:
    ```bash
    nix hash convert --hash-algo "sha1" --to sri "800d59cfcd3c05e900cb4e214be48f6b886a08df"
    sha1-gA1Zz808BekAy04hS+SPa4hqCN8=
    ```
  - Assert the input format of the hash:
    ```bash
    nix hash convert --hash-algo "sha256" --from nix32 "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="
    error: input hash 'ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=' does not have the expected format '--from nix32'
    nix hash convert --hash-algo "sha256" --from nix32 "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s"
    sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=
    ```

  The `--to`/`--from`/`--hash-algo` parameters have context-sensitive auto-completion.

  ## Related Deprecations

  The following commands are still available but will emit a deprecation warning. Please convert your code to
  `nix hash convert`:

  - `nix hash to-base16 $hash1 $hash2`: Use `nix hash convert --to base16 $hash1 $hash2` instead.
  - `nix hash to-base32 $hash1 $hash2`: Use `nix hash convert --to nix32 $hash1 $hash2` instead.
  - `nix hash to-base64 $hash1 $hash2`: Use `nix hash convert --to base64 $hash1 $hash2` instead.
  - `nix hash to-sri $hash1 $hash2`: : Use `nix hash convert --to sri $hash1 $hash2`
    or even just `nix hash convert $hash1 $hash2` instead.

- `nix profile` now allows referring to elements by human-readable name [#8678](https://github.com/NixOS/nix/pull/8678)

  [`nix profile`](@docroot@/command-ref/new-cli/nix3-profile.md) now uses names to refer to installed packages when running [`list`](@docroot@/command-ref/new-cli/nix3-profile-list.md), [`remove`](@docroot@/command-ref/new-cli/nix3-profile-remove.md) or [`upgrade`](@docroot@/command-ref/new-cli/nix3-profile-upgrade.md) as opposed to indices. Profile element names are generated when a package is installed and remain the same until the package is removed.

  **Warning**: The `manifest.nix` file used to record the contents of profiles has changed. Nix will automatically upgrade profiles to the new version when you modify the profile. After that, the profile can no longer be used by older versions of Nix.

- Rename hash format `base32` to `nix32` [#8678](https://github.com/NixOS/nix/pull/8678)

  Hash format `base32` was renamed to `nix32` since it used a special nix-specific character set for
  [Base32](https://en.wikipedia.org/wiki/Base32).

  ## Deprecation: Use `nix32` instead of `base32` as `toHashFormat`

  For the builtin `convertHash`, the `toHashFormat` parameter now accepts the same hash formats as the `--to`/`--from`
  parameters of the `nix hash conert` command: `"base16"`, `"nix32"`, `"base64"`, and `"sri"`. The former `"base32"` value
  remains as a deprecated alias for `"base32"`. Please convert your code from:

  ```nix
  builtins.convertHash { inherit hash hashAlgo; toHashFormat = "base32";}
  ```

  to

  ```nix
  builtins.convertHash { inherit hash hashAlgo; toHashFormat = "nix32";}
  ```

- Give `nix store add` a `--hash-algo` flag [#9809](https://github.com/NixOS/nix/pull/9809)

  Adds a missing feature that was present in the old CLI, and matches our
  plans to have similar flags for `nix hash convert` and `nix hash path`.

- Coercion errors include the failing value

  The `error: cannot coerce a <TYPE> to a string` message now includes the value
  which caused the error.

  Before:

  ```
         error: cannot coerce a set to a string
  ```

  After:

  ```
         error: cannot coerce a set to a string: { aesSupport = «thunk»;
         avx2Support = «thunk»; avx512Support = «thunk»; avxSupport = «thunk»;
         canExecute = «thunk»; config = «thunk»; darwinArch = «thunk»; darwinMinVersion
         = «thunk»; darwinMinVersionVariable = «thunk»; darwinPlatform = «thunk»; «84
         attributes elided»}
  ```

- Type errors include the failing value

  In errors like `value is an integer while a list was expected`, the message now
  includes the failing value.

  Before:

  ```
         error: value is a set while a string was expected
  ```

  After:

  ```
         error: expected a string but found a set: { ghc810 = «thunk»;
         ghc8102Binary = «thunk»; ghc8107 = «thunk»; ghc8107Binary = «thunk»;
         ghc865Binary = «thunk»; ghc90 = «thunk»; ghc902 = «thunk»; ghc92 = «thunk»;
         ghc924Binary = «thunk»; ghc925 = «thunk»;  «17 attributes elided»}
  ```

- Source locations are printed more consistently in errors [#561](https://github.com/NixOS/nix/issues/561) [#9555](https://github.com/NixOS/nix/pull/9555)

  Source location information is now included in error messages more
  consistently. Given this code:

  ```nix
  let
    attr = {foo = "bar";};
    key = {};
  in
    attr.${key}
  ```

  Previously, Nix would show this unhelpful message when attempting to evaluate
  it:

  ```
  error:
         … while evaluating an attribute name

         error: value is a set while a string was expected
  ```

  Now, the error message displays where the problematic value was found:

  ```
  error:
         … while evaluating an attribute name

           at bad.nix:4:11:

              3|   key = {};
              4| in attr.${key}
               |           ^
              5|

         error: expected a string but found a set
  ```

- Some stack overflow segfaults are fixed [#9616](https://github.com/NixOS/nix/issues/9616) [#9617](https://github.com/NixOS/nix/pull/9617)

  The number of nested function calls has been restricted, to detect and report
  infinite function call recursions. The default maximum call depth is 10,000 and
  can be set with [the `max-call-depth`
  option](@docroot@/command-ref/conf-file.md#conf-max-call-depth).

  This fixes segfaults or the following unhelpful error message in many cases:

      error: stack overflow (possible infinite recursion)

  Before:

  ```
  $ nix-instantiate --eval --expr '(x: x x) (x: x x)'
  Segmentation fault: 11
  ```

  After:

  ```
  $ nix-instantiate --eval --expr '(x: x x) (x: x x)'
  error: stack overflow

         at «string»:1:14:
              1| (x: x x) (x: x x)
               |              ^
  ```

- Better error reporting for `with` expressions [#9658](https://github.com/NixOS/nix/pull/9658)

  `with` expressions using non-attrset values to resolve variables are now reported with proper positions.

  Previously an incorrect `with` expression would report no position at all, making it hard to determine where the error originated:

  ```
  nix-repl> with 1; a
  error:
         … <borked>

           at «none»:0: (source not available)

         error: value is an integer while a set was expected
  ```

  Now position information is preserved and reported as with most other errors:

  ```
  nix-repl> with 1; a
  error:
         … while evaluating the first subexpression of a with expression
           at «string»:1:1:
              1| with 1; a
               | ^

         error: expected a set but found an integer
  ```

