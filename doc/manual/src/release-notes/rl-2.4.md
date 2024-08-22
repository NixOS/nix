# Release 2.4 (2021-11-01)

This is the first release in more than two years and is the result of
more than 2800 commits from 195 contributors since release 2.3.

## Highlights

* Nix's **error messages** have been improved a lot. For instance,
  evaluation errors now point out the location of the error:

  ```
  $ nix build
  error: undefined variable 'bzip3'

         at /nix/store/449lv242z0zsgwv95a8124xi11sp419f-source/flake.nix:88:13:

             87|           [ curl
             88|             bzip3 xz brotli editline
               |             ^
             89|             openssl sqlite
  ```

* The **`nix` command** has seen a lot of work and is now almost at
  feature parity with the old command-line interface (the `nix-*`
  commands). It aims to be [more modern, consistent and pleasant to
  use](../development/cli-guideline.md) than the old CLI. It is still
  marked as experimental but its interface should not change much
  anymore in future releases.

* **Flakes** are a new format to package Nix-based projects in a more
  discoverable, composable, consistent and reproducible way. A flake
  is just a repository or tarball containing a file named `flake.nix`
  that specifies dependencies on other flakes and returns any Nix
  assets such as packages, Nixpkgs overlays, NixOS modules or CI
  tests. The new `nix` CLI is primarily based around flakes; for
  example, a command like `nix run nixpkgs#hello` runs the `hello`
  application from the `nixpkgs` flake.

  Flakes are currently marked as experimental. For an introduction,
  see [this blog
  post](https://www.tweag.io/blog/2020-05-25-flakes/). For detailed
  information about flake syntax and semantics, see the [`nix flake`
  manual page](../command-ref/new-cli/nix3-flake.md).

* Nix's store can now be **content-addressed**, meaning that the hash
  component of a store path is the hash of the path's
  contents. Previously Nix could only build **input-addressed** store
  paths, where the hash is computed from the derivation dependency
  graph. Content-addressing allows deduplication, early cutoff in
  build systems, and unprivileged closure copying. This is still [an
  experimental
  feature](https://discourse.nixos.org/t/content-addressed-nix-call-for-testers/12881).

* The Nix manual has been converted into Markdown, making it easier to
  contribute. In addition, every `nix` subcommand now has a manual
  page, documenting every option.

* A new setting that allows **experimental features** to be enabled
  selectively. This allows us to merge unstable features into Nix more
  quickly and do more frequent releases.

## Other features

* There are many new `nix` subcommands:

  - `nix develop` is intended to replace `nix-shell`. It has a number
    of new features:

    * It automatically sets the output environment variables (such as
      `$out`) to writable locations (such as `./outputs/out`).

    * It can store the environment in a profile. This is useful for
      offline work.

    * It can run specific phases directly. For instance, `nix develop
      --build` runs `buildPhase`.

    - It allows dependencies in the Nix store to be "redirected" to
      arbitrary directories using the `--redirect` flag. This is
      useful if you want to hack on a package *and* some of its
      dependencies at the same time.

  - `nix print-dev-env` prints the environment variables and bash
    functions defined by a derivation. This is useful for users of
    other shells than bash (especially with `--json`).

  - `nix shell` was previously named `nix run` and is intended to
    replace `nix-shell -p`, but without the `stdenv` overhead. It
    simply starts a shell where some packages have been added to
    `$PATH`.

  - `nix run` (not to be confused with the old subcommand that has
    been renamed to `nix shell`) runs an "app", a flake output that
    specifies a command to run, or an eponymous program from a
    package. For example, `nix run nixpkgs#hello` runs the `hello`
    program from the `hello` package in `nixpkgs`.

  - `nix flake` is the container for flake-related operations, such as
    creating a new flake, querying the contents of a flake or updating
    flake lock files.

  - `nix registry` allows you to query and update the flake registry,
    which maps identifiers such as `nixpkgs` to concrete flake URLs.

  - `nix profile` is intended to replace `nix-env`. Its main advantage
    is that it keeps track of the provenance of installed packages
    (e.g. exactly which flake version a package came from). It also
    has some helpful subcommands:

    * `nix profile history` shows what packages were added, upgraded
      or removed between each version of a profile.

    * `nix profile diff-closures` shows the changes between the
      closures of each version of a profile. This allows you to
      discover the addition or removal of dependencies or size
      changes.

    **Warning**: after a profile has been updated using `nix profile`,
    it is no longer usable with `nix-env`.

  - `nix store diff-closures` shows the differences between the
    closures of two store paths in terms of the versions and sizes of
    dependencies in the closures.

  - `nix store make-content-addressable` rewrites an arbitrary closure
    to make it content-addressed. Such paths can be copied into other
    stores without requiring signatures.

  - `nix bundle` uses the [`nix-bundle`
    program](https://github.com/matthewbauer/nix-bundle) to convert a
    closure into a self-extracting executable.

  - Various other replacements for the old CLI, e.g. `nix store gc`,
    `nix store delete`, `nix store repair`, `nix nar dump-path`, `nix
    store prefetch-file`, `nix store prefetch-tarball`, `nix key` and
    `nix daemon`.

* Nix now has an **evaluation cache** for flake outputs. For example,
  a second invocation of the command `nix run nixpkgs#firefox` will
  not need to evaluate the `firefox` attribute because it's already in
  the evaluation cache. This is made possible by the hermetic
  evaluation model of flakes.

  Intermediate results are not cached.

* The new `--offline` flag disables substituters and causes all
  locally cached tarballs and repositories to be considered
  up-to-date.

* The new `--refresh` flag causes all locally cached tarballs and
  repositories to be considered out-of-date.

* Many `nix` subcommands now have a `--json` option to produce
  machine-readable output.

* `nix repl` has a new `:doc` command to show documentation about
  builtin functions (e.g. `:doc builtins.map`).

* Binary cache stores now have an option `index-debug-info` to create
  an index of DWARF debuginfo files for use by
  [`dwarffs`](https://github.com/edolstra/dwarffs).

* To support flakes, Nix now has an extensible mechanism for fetching
  source trees. Currently it has the following backends:

  * Git repositories

  * Mercurial repositories

  * GitHub and GitLab repositories (an optimisation for faster
    fetching than Git)

  * Tarballs

  * Arbitrary directories

  The fetcher infrastructure is exposed via flake input specifications
  and via the `fetchTree` built-in.

* **Languages changes**: the only new language feature is that you can
  now have antiquotations in paths, e.g. `./${foo}` instead of `./. +
  foo`.

* **New built-in functions**:

  - `builtins.fetchTree` allows fetching a source tree using any
    backends supported by the fetcher infrastructure. It subsumes the
    functionality of existing built-ins like `fetchGit`,
    `fetchMercurial` and `fetchTarball`.

  - `builtins.getFlake` fetches a flake and returns its output
    attributes. This function should not be used inside flakes! Use
    flake inputs instead.

  - `builtins.floor` and `builtins.ceil` round a floating-point number
    down and up, respectively.

* Experimental support for recursive Nix. This means that Nix
  derivations can now call Nix to build other derivations. This is not
  in a stable state yet and not well
  [documented](https://github.com/NixOS/nix/commit/c4d7c76b641d82b2696fef73ce0ac160043c18da).

* The new experimental feature `no-url-literals` disables URL
  literals. This helps to implement [RFC
  45](https://github.com/NixOS/rfcs/pull/45).

* Nix now uses `libarchive` to decompress and unpack tarballs and zip
  files, so `tar` is no longer required.

* The priority of substituters can now be overridden using the
  `priority` substituter setting (e.g. `--substituters
  'http://cache.nixos.org?priority=100 daemon?priority=10'`).

* `nix edit` now supports non-derivation attributes, e.g. `nix edit
  .#nixosConfigurations.bla`.

* The `nix` command now provides command line completion for `bash`,
  `zsh` and `fish`. Since the support for getting completions is built
  into `nix`, it's easy to add support for other shells.

* The new `--log-format` flag selects what Nix's output looks like. It
  defaults to a terse progress indicator. There is a new
  `internal-json` output format for use by other programs.

* `nix eval` has a new `--apply` flag that applies a function to the
  evaluation result.

* `nix eval` has a new `--write-to` flag that allows it to write a
  nested attribute set of string leaves to a corresponding directory
  tree.

* Memory improvements: many operations that add paths to the store or
  copy paths between stores now run in constant memory.

* Many `nix` commands now support the flag `--derivation` to operate
  on a `.drv` file itself instead of its outputs.

* There is a new store called `dummy://` that does not support
  building or adding paths. This is useful if you want to use the Nix
  evaluator but don't have a Nix store.

* The `ssh-ng://` store now allows substituting paths on the remote,
  as `ssh://` already did.

* When auto-calling a function with an ellipsis, all arguments are now
  passed.

* New `nix-shell` features:

  - It preserves the `PS1` environment variable if
    `NIX_SHELL_PRESERVE_PROMPT` is set.

  - With `-p`, it passes any `--arg`s as Nixpkgs arguments.

  - Support for structured attributes.

* `nix-prefetch-url` has a new `--executable` flag.

* On `x86_64` systems, [`x86_64` microarchitecture
  levels](https://lwn.net/Articles/844831/) are mapped to additional
  system types (e.g. `x86_64-v1-linux`).

* The new `--eval-store` flag allows you to use a different store for
  evaluation than for building or storing the build result. This is
  primarily useful when you want to query whether something exists in
  a read-only store, such as a binary cache:

  ```
  # nix path-info --json --store https://cache.nixos.org \
    --eval-store auto nixpkgs#hello
  ```

  (Here `auto` indicates the local store.)

* The Nix daemon has a new low-latency mechanism for copying
  closures. This is useful when building on remote stores such as
  `ssh-ng://`.

* Plugins can now register `nix` subcommands.

* The `--indirect` flag to `nix-store --add-root` has become a no-op.
  `--add-root` will always generate indirect GC roots from now on.

## Incompatible changes

* The `nix` command is now marked as an experimental feature. This
  means that you need to add

  ```
  experimental-features = nix-command
  ```

  to your `nix.conf` if you want to use it, or pass
  `--extra-experimental-features nix-command` on the command line.

* The `nix` command no longer has a syntax for referring to packages
  in a channel. This means that the following no longer works:

  ```console
  nix build nixpkgs.hello # Nix 2.3
  ```

  Instead, you can either use the `#` syntax to select a package from
  a flake, e.g.

  ```console
  nix build nixpkgs#hello
  ```

  Or, if you want to use the `nixpkgs` channel in the `NIX_PATH`
  environment variable:

  ```console
  nix build -f '<nixpkgs>' hello
  ```

* The old `nix run` has been renamed to `nix shell`, while there is a
  new `nix run` that runs a default command. So instead of

  ```console
  nix run nixpkgs.hello -c hello # Nix 2.3
  ```

  you should use

  ```console
  nix shell nixpkgs#hello -c hello
  ```

  or just

  ```console
  nix run nixpkgs#hello
  ```

  if the command you want to run has the same name as the package.

* It is now an error to modify the `plugin-files` setting via a
  command-line flag that appears after the first non-flag argument to
  any command, including a subcommand to `nix`. For example,
  `nix-instantiate default.nix --plugin-files ""` must now become
  `nix-instantiate --plugin-files "" default.nix`.

* We no longer release source tarballs. If you want to build from
  source, please build from the tags in the Git repository.

## Contributors

This release has contributions from
Adam Höse,
Albert Safin,
Alex Kovar,
Alex Zero,
Alexander Bantyev,
Alexandre Esteves,
Alyssa Ross,
Anatole Lucet,
Anders Kaseorg,
Andreas Rammhold,
Antoine Eiche,
Antoine Martin,
Arnout Engelen,
Arthur Gautier,
aszlig,
Ben Burdette,
Benjamin Hipple,
Bernardo Meurer,
Björn Gohla,
Bjørn Forsman,
Bob van der Linden,
Brian Leung,
Brian McKenna,
Brian Wignall,
Bruce Toll,
Bryan Richter,
Calle Rosenquist,
Calvin Loncaric,
Carlo Nucera,
Carlos D'Agostino,
Chaz Schlarp,
Christian Höppner,
Christian Kampka,
Chua Hou,
Chuck,
Cole Helbling,
Daiderd Jordan,
Dan Callahan,
Dani,
Daniel Fitzpatrick,
Danila Fedorin,
Daniël de Kok,
Danny Bautista,
DavHau,
David McFarland,
Dima,
Domen Kožar,
Dominik Schrempf,
Dominique Martinet,
dramforever,
Dustin DeWeese,
edef,
Eelco Dolstra,
Ellie Hermaszewska,
Emilio Karakey,
Emily,
Eric Culp,
Ersin Akinci,
Fabian Möller,
Farid Zakaria,
Federico Pellegrin,
Finn Behrens,
Florian Franzen,
Félix Baylac-Jacqué,
Gabriella Gonzalez,
Geoff Reedy,
Georges Dubus,
Graham Christensen,
Greg Hale,
Greg Price,
Gregor Kleen,
Gregory Hale,
Griffin Smith,
Guillaume Bouchard,
Harald van Dijk,
illustris,
Ivan Zvonimir Horvat,
Jade,
Jake Waksbaum,
jakobrs,
James Ottaway,
Jan Tojnar,
Janne Heß,
Jaroslavas Pocepko,
Jarrett Keifer,
Jeremy Schlatter,
Joachim Breitner,
Joe Pea,
John Ericson,
Jonathan Ringer,
Josef Kemetmüller,
Joseph Lucas,
Jude Taylor,
Julian Stecklina,
Julien Tanguy,
Jörg Thalheim,
Kai Wohlfahrt,
keke,
Keshav Kini,
Kevin Quick,
Kevin Stock,
Kjetil Orbekk,
Krzysztof Gogolewski,
kvtb,
Lars Mühmel,
Leonhard Markert,
Lily Ballard,
Linus Heckemann,
Lorenzo Manacorda,
Lucas Desgouilles,
Lucas Franceschino,
Lucas Hoffmann,
Luke Granger-Brown,
Madeline Haraj,
Marwan Aljubeh,
Mat Marini,
Mateusz Piotrowski,
Matthew Bauer,
Matthew Kenigsberg,
Mauricio Scheffer,
Maximilian Bosch,
Michael Adler,
Michael Bishop,
Michael Fellinger,
Michael Forney,
Michael Reilly,
mlatus,
Mykola Orliuk,
Nathan van Doorn,
Naïm Favier,
ng0,
Nick Van den Broeck,
Nicolas Stig124 Formichella,
Niels Egberts,
Niklas Hambüchen,
Nikola Knezevic,
oxalica,
p01arst0rm,
Pamplemousse,
Patrick Hilhorst,
Paul Opiyo,
Pavol Rusnak,
Peter Kolloch,
Philipp Bartsch,
Philipp Middendorf,
Piotr Szubiakowski,
Profpatsch,
Puck Meerburg,
Ricardo M. Correia,
Rickard Nilsson,
Robert Hensing,
Robin Gloster,
Rodrigo,
Rok Garbas,
Ronnie Ebrin,
Rovanion Luckey,
Ryan Burns,
Ryan Mulligan,
Ryne Everett,
Sam Doshi,
Sam Lidder,
Samir Talwar,
Samuel Dionne-Riel,
Sebastian Ullrich,
Sergei Trofimovich,
Sevan Janiyan,
Shao Cheng,
Shea Levy,
Silvan Mosberger,
Stefan Frijters,
Stefan Jaax,
sternenseemann,
Steven Shaw,
Stéphan Kochen,
SuperSandro2000,
Suraj Barkale,
Taeer Bar-Yam,
Thomas Churchman,
Théophane Hufschmitt,
Timothy DeHerrera,
Timothy Klim,
Tobias Möst,
Tobias Pflug,
Tom Bereknyei,
Travis A. Everett,
Ujjwal Jain,
Vladimír Čunát,
Wil Taylor,
Will Dietz,
Yaroslav Bolyukin,
Yestin L. Harrison,
YI,
Yorick van Pelt,
Yuriy Taraday and
zimbatm.
