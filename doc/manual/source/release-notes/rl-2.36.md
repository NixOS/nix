# Release 2.36.0 (2026-11-16)

## Improvements

- Daemon worker spawning rework [#15857](https://github.com/NixOS/nix/issues/15857) [#16659](https://github.com/NixOS/nix/pull/16659)

  Previously, `nix-daemon` accepting on the `nix-daemon.socket` would spawn a worker process by `fork()`-ing without calling `execv*`.
  This has historically been a source of issues around fork-unsafe dependencies and in Nix itself.
  The most notorious instance of this issue has been Nix on macOS, which discourages such use of fork(2),
  and has caused numerous issues in the past (mostly around the use of `SecureTransport` TLS backend in libcurl and aws-c-io).

  Now `nix-daemon` spawns a worker processes by spawning and executing itself in `--stdio` mode by redirecting stdin/stdout
  to the accepted socket and forcing the trust level with the stabilised `--force-trusted` and `--force-untrusted` flags.
  The path to the current binary is either `/proc/self/exe` on Linux, acquired via `_NSGetExecutablePath` on macOS, or `KERN_PROC_PATHNAME` sysctl on FreeBSD.

  No functional changes are expected from this, though there are minor user-visible behavioral changes:

  - `nix-daemon` no longer needs to be restarted for `/etc/nix/nix.conf` changes to take effect in the daemon *worker*
    (note that this doesn't apply to settings like `allowed-users` or `trusted-users`).
  - The command line of the worker process no longer has the form `nix-daemon <peer pid>`.
    Previously, the PID of the daemon client process was inserted into `argv[1]`.
    This also obviates the need for passing the no-op `--daemon` flag for making room for the PID in `argv[1]`.
    The peer PID is now part of the `argv[0]` in the form of `nix-daemon.worker-<peer pid>`.
    This is an internal debugging feature used for correlating the daemon *worker* process with its client.

- `daemon-trust-override`, `--process-ops` stabilisation
   [#16637](https://github.com/NixOS/nix/pull/16637) [#16659](https://github.com/NixOS/nix/pull/16659)

  `daemon-trust-override` experimental feature has been stabilised.
  This change was needed to facilitate the daemon worker spawning rework.

  `--force-untrusted` now behaves correctly when the `--stdio` daemon worker's store is another remote store backed by another daemon.

  `--force-untrusted`, `--force-trusted`, `--default-trust`, `--process-ops` flags accepted by `nix-daemon` and `nix daemon` are no longer experimental.

- Daemon enables cgroup controllers for build cgroups [#9675](https://github.com/NixOS/nix/issues/9675) [#16024](https://github.com/NixOS/nix/pull/16024)

  With `use-cgroups`, the daemon now enables the `cpu`, `memory`, `io` and `pids`
  controllers in its cgroup's `cgroup.subtree_control`, so per-build cgroups
  expose `memory.peak`, `io.stat` etc. instead of only `cpu.stat`. This requires
  the controllers to be delegated to the daemon's cgroup (e.g. systemd
  `Delegate=yes`).

- Nix now finds Darwin runtime garbage collector roots without `lsof` [#3011](https://github.com/NixOS/nix/issues/3011) [#13990](https://github.com/NixOS/nix/issues/13990)

  On Darwin, the garbage collector now uses `libproc` to retain store paths
  referenced by running processes through their working directories, open files,
  mapped regions, or environment variables. This avoids the slow `lsof` fallback
  and adds environment-variable roots, which `lsof` could not discover.

  macOS does not expose the environment variables of entitled binaries while
  System Integrity Protection is enabled, so those roots remain unavailable.

- `nix run`, `nix search` and `nix bundle` now auto-call functions [#5316](https://github.com/NixOS/nix/issues/5316) [#15895](https://github.com/NixOS/nix/pull/15895)

  If an [installable](@docroot@/glossary.md#gloss-installable) given with `--file` or `--expr` evaluates to a function, it is now called automatically, provided each of its arguments has a default value or is passed with `--arg`/`--argstr`.
  This makes `nix run`, `nix search` and `nix bundle` behave like `nix build`, `nix shell`, `nix-build` and `nix-env --query`.

  For example, `nix run --impure --expr '{ pkgs ? import <nixpkgs> {} }: pkgs.hello'` now works instead of failing with `error: attribute 'type' does not exist`.

  `nix eval` and `nix edit`, which show the value itself rather than a package, are unchanged.

- Build cgroups are named after the derivation [#16516](https://github.com/NixOS/nix/pull/16516)

  Per-build cgroups are now named `nix-build@<drv-hash>-<uid>` when a build user is used, and `nix-build@<drv-hash>-<pid>` otherwise.
  They replace `nix-build-uid-<uid>` and `nix-build-pid-<pid>-<counter>`, so external tools can locate the cgroup of a given derivation's build.

- Add an alias for optimise. [#1902](https://github.com/NixOS/nix/issues/1902) [#11326](https://github.com/NixOS/nix/issues/11326) [#11577](https://github.com/NixOS/nix/pull/11577) [#15997](https://github.com/NixOS/nix/pull/15997)

  Add "optimize" as a linguistic variation of "optimise". Now both the `nix-store` and `nix store` commands work with the same spelling.

- Styled output on stdout is plain when stdout is not a terminal [#4626](https://github.com/NixOS/nix/issues/4626) [#9486](https://github.com/NixOS/nix/issues/9486) [#16710](https://github.com/NixOS/nix/issues/16710) [#16711](https://github.com/NixOS/nix/pull/16711)

  `nix profile list`, `nix profile history`, `nix flake metadata`, `nix flake show`, `nix search` and `nix store diff-closures` no longer write ANSI bold and color to stdout when it is not a terminal, or when `NO_COLOR` is set.
  Set `CLICOLOR_FORCE` to keep the styling.

## Error message improvements

- Start of infinite recursion is now visible [#16296](https://github.com/NixOS/nix/pull/16296)

  When Nix reports a trace for an infinitely recursive evaluation,
  it will now let you discern the cycle from the code that leads up to the cycle.

  Example:

  `nix eval --expr 'let f = builtins.add f 1; in toString f' --show-trace`
  ```
  error:
         … while calling the 'toString' builtin
           at «string»:1:30:
              1| let f = builtins.add f 1; in toString f
               |                              ^

         … entering the infinite recursion

         … while calling the 'add' builtin
           at «string»:1:9:
              1| let f = builtins.add f 1; in toString f
               |         ^

         error: infinite recursion encountered
  ```

- `builtins.warn` now supports unindented multi-line messages [#14794](https://github.com/NixOS/nix/pull/14794)

  `builtins.warn` now preserves the formatting of messages that start with a newline instead of adding indentation to align them with the warning prefix.

- `builtins.addErrorContext` keeps the error from *value* when *context* fails [#8283](https://github.com/NixOS/nix/issues/8283) [#16618](https://github.com/NixOS/nix/pull/16618)

  If evaluating *context* in [`builtins.addErrorContext context value`](@docroot@/language/builtins.md#builtins-addErrorContext) fails, the error from *value* now propagates, with the error from *context* in its trace.
  Library authors can therefore use more complex *context* expressions without the risk of hiding the error from *value*.

  `addErrorContext` can no longer be abused to catch uncatchable errors like `abort`.

## Bug fixes

- `builtins.pathExists` no longer swallows access errors from evaluating its argument [#16728](https://github.com/NixOS/nix/pull/16728)

  Since Nix 2.20, `builtins.pathExists` accidentally recovered from access errors caused by evaluating its argument. Consider:

  ```sh
  $ nix eval --expr 'builtins.pathExists (builtins.readFile /etc/passwd)'
  ```

  This evaluated to `false`, even though `builtins.readFile /etc/passwd` would throw, and `builtins.pathExists` never saw a supposed path.

  Now the error propagates, as it did in Nix 2.19 and earlier. This invocation fails with the same error observed by calling `builtins.readFile /etc/passwd` in isolation.

- `nix eval --write-to` is stricter about what it writes [#16760](https://github.com/NixOS/nix/pull/16760)

  `nix eval --write-to` now requires attributes to correspond to a single file name.
  In particular, it rejects attribute names that contain `/` or are empty, in addition to rejecting `.` and `..` as it did already.
  Previously, an attribute such as `{ "a/b" = "..."; }` was written to a nested path `a/b`, which is a bit of an "injection".

  Attribute sets can also now be nested at most 64 levels deep, the same limit as for NARs.

  (The output is also now written in the same way as NARs are unpacked, which hardens it in a bunch of ways.)

- Path-like flake references detect Mercurial checkouts

  A path-like flake reference such as `.` used to become a `git+file:` input only inside a Git repository, and a `path:` input everywhere else.
  In a Mercurial checkout that copied the whole directory to the store, `.hg` and untracked files included, and the flake had no `rev`.

  Such a reference now resolves to `hg+file:`, so only tracked files are copied.
  This needs the `hg` executable, as `hg+file:` inputs always did.

  Input schemes, including ones from [`plugin-files`](@docroot@/command-ref/conf-file.md#conf-plugin-files), can resolve a path-like flake reference to their own scheme by implementing `InputScheme::localRepoURL()`.

- S3: `endpoint=host:port` fixed, deprecated in favour of full URLs

  Bare `host:port` values for the `endpoint` parameter of `s3://` store URLs
  were misparsed (the host was taken as a URL scheme). This is fixed, but the
  bare form and the separate `scheme` parameter are deprecated: use a full URL
  such as `endpoint=https://minio.example.com` instead.

## Contributors

This release was made possible by the following 68 contributors:

- Angel J [**(@iamanaws)**](https://github.com/iamanaws)
- saara [**(@misspenalty)**](https://github.com/misspenalty)
- Krithik Rao [**(@KritR)**](https://github.com/KritR)
- Michael Daniels [**(@mdaniels5757)**](https://github.com/mdaniels5757)
- Kip Landergren [**(@klandergren)**](https://github.com/klandergren)
- Lisanna Dettwyler [**(@lisanna-dettwyler)**](https://github.com/lisanna-dettwyler)
- Vladimir Petrzhikovskii [**(@0xdeafbeef)**](https://github.com/0xdeafbeef)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- figsoda [**(@figsoda)**](https://github.com/figsoda)
- raf [**(@NotAShelf)**](https://github.com/NotAShelf)
- Amaan Qureshi [**(@amaanq)**](https://github.com/amaanq)
- Jazzkid [**(@Jazzkid0)**](https://github.com/Jazzkid0)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- StepBroBD [**(@stepbrobd)**](https://github.com/stepbrobd)
- dram [**(@dramforever)**](https://github.com/dramforever)
- Jonas Chevalier [**(@zimbatm)**](https://github.com/zimbatm)
- yunfachi [**(@yunfachi)**](https://github.com/yunfachi)
- Matthew Kenigsberg [**(@mkenigs)**](https://github.com/mkenigs)
- Jake Hillion [**(@JakeHillion)**](https://github.com/JakeHillion)
- Marshall Porter [**(@meop)**](https://github.com/meop)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Philip Taron [**(@PhilipTaronQ)**](https://github.com/PhilipTaronQ)
- Anton Kesy [**(@antonkesy)**](https://github.com/antonkesy)
- Jens Petersen [**(@juhp)**](https://github.com/juhp)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- HHH [**(@H7TPS)**](https://github.com/H7TPS)
- Ihar Hrachyshka [**(@booxter)**](https://github.com/booxter)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Sandro [**(@SuperSandro2000)**](https://github.com/SuperSandro2000)
- faukah [**(@faukah)**](https://github.com/faukah)
- Rohit Singh [**(@sinrohit-desco)**](https://github.com/sinrohit-desco)
- Artemis Tosini [**(@artemist)**](https://github.com/artemist)
- Gaétan Lepage [**(@GaetanLepage)**](https://github.com/GaetanLepage)
- Yaroslav Kuznetsov [**(@YENGISAY)**](https://github.com/YENGISAY)
- Bang Lee [**(@Qusic)**](https://github.com/Qusic)
- Francesco Gazzetta [**(@fgaz)**](https://github.com/fgaz)
- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- reee [**(@qsxDree)**](https://github.com/qsxDree)
- Evan Quiney [**(@Evanev7)**](https://github.com/Evanev7)
- Cosima Neidahl [**(@OPNA2608)**](https://github.com/OPNA2608)
- Domen Kožar [**(@domenkozar)**](https://github.com/domenkozar)
- Jacek Galowicz [**(@tfc)**](https://github.com/tfc)
- sulewicz-gq [**(@sulewicz-gq)**](https://github.com/sulewicz-gq)
- Grayson Tinker [**(@GraysonTinker)**](https://github.com/GraysonTinker)
- fsagbuya [**(@fsagbuya)**](https://github.com/fsagbuya)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Chad Zawistowski [**(@czawistowski-nvidia)**](https://github.com/czawistowski-nvidia)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- adisbladis [**(@adisbladis)**](https://github.com/adisbladis)
- Dennis [**(@DerDennisOP)**](https://github.com/DerDennisOP)
- aetos [**(@aetosdios27)**](https://github.com/aetosdios27)
- Zapolyarny [**(@ZapolyarnyDev)**](https://github.com/ZapolyarnyDev)
- Yuta Fudaba [**(@ph0ryn)**](https://github.com/ph0ryn)
- Carl [**(@Lillecarl)**](https://github.com/Lillecarl)
- Riccardo Mazzarini [**(@noib3)**](https://github.com/noib3)
- Jonathan De Troye [**(@detroyejr)**](https://github.com/detroyejr)
- ulucs [**(@ulucs)**](https://github.com/ulucs)
- Jamie Magee [**(@JamieMagee)**](https://github.com/JamieMagee)
- Filippo [**(@Filippo-Galli)**](https://github.com/Filippo-Galli)
- Adam Dinwoodie [**(@me-and)**](https://github.com/me-and)
- Jeremy Fleischman [**(@jfly)**](https://github.com/jfly)
- Philip Taron [**(@philiptaron)**](https://github.com/philiptaron)
- Thiago Kenji Okada [**(@thiagokokada)**](https://github.com/thiagokokada)
- Alex [**(@filaka771)**](https://github.com/filaka771)
- Ryan Hendrickson [**(@rhendric)**](https://github.com/rhendric)
- Michael Vorburger [**(@vorburger)**](https://github.com/vorburger)
- awsmadi [**(@awsmadi)**](https://github.com/awsmadi)
- Philipp Otterbein
