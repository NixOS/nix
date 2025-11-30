# Release 2.26.0 (2025-01-22)

- Support for relative path inputs [#10089](https://github.com/NixOS/nix/pull/10089)

  Flakes can now refer to other flakes in the same repository using relative paths, e.g.
  ```nix
  inputs.foo.url = "path:./foo";
  ```
  uses the flake in the `foo` subdirectory of the referring flake. For more information, see the documentation on [the `path` flake input type](@docroot@/command-ref/new-cli/nix3-flake.md#path-fetcher).

  This feature required a change to the lock file format. Previous Nix versions will not be able to use lock files that have locks for relative path inputs in them.

- Flake lock file generation now ignores local registries [#12019](https://github.com/NixOS/nix/pull/12019)

  When resolving indirect flake references like `nixpkgs` in `flake.nix` files, Nix will no longer use the system and user flake registries. It will only use the global flake registry and overrides given on the command line via `--override-flake`.

  This avoids accidents where users have local registry overrides that map `nixpkgs` to a `path:` flake in the local file system, which then end up in committed lock files pushed to other users.

  In the future, we may remove the use of the registry during lock file generation altogether. It's better to explicitly specify the URL of a flake input. For example, instead of
  ```nix
  {
    outputs = { self, nixpkgs }: { ... };
  }
  ```
  write
  ```nix
  {
    inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    outputs = { self, nixpkgs }: { ... };
  }
  ```

- `nix copy` supports `--profile` and `--out-link` [#11657](https://github.com/NixOS/nix/pull/11657)

  The `nix copy` command now has flags `--profile` and `--out-link`, similar to `nix build`. `--profile` makes a profile point to the
  top-level store path, while `--out-link` create symlinks to the top-level store paths.

  For example, when updating the local NixOS system profile from a NixOS system closure on a remote machine, instead of
  ```
  # nix copy --from ssh://server $path
  # nix build --profile /nix/var/nix/profiles/system $path
  ```
  you can now do
  ```
  # nix copy --from ssh://server --profile /nix/var/nix/profiles/system $path
  ```
  The advantage is that this avoids a time window where *path* is not a garbage collector root, and so could be deleted by a concurrent `nix store gc` process.

- `nix-instantiate --eval` now supports `--raw` [#12119](https://github.com/NixOS/nix/pull/12119)

  The `nix-instantiate --eval` command now supports a `--raw` flag, when used
  the evaluation result must be a string, which is printed verbatim without
  quotation marks or escaping.

- Improved `NIX_SSHOPTS` parsing for better SSH option handling [#5181](https://github.com/NixOS/nix/issues/5181) [#12020](https://github.com/NixOS/nix/pull/12020)

  The parsing of the `NIX_SSHOPTS` environment variable has been improved to handle spaces and quotes correctly.
  Previously, incorrectly split SSH options could cause failures in commands like `nix-copy-closure`,
  especially when using complex SSH invocations such as `-o ProxyCommand="ssh -W %h:%p ..."`.

  This change introduces a `shellSplitString` function to ensure
  that `NIX_SSHOPTS` is parsed in a manner consistent with shell
  behavior, addressing common parsing errors.

  For example, the following now works as expected:

  ```bash
  export NIX_SSHOPTS='-o ProxyCommand="ssh -W %h:%p ..."'
  ```

  This update improves the reliability of SSH-related operations using `NIX_SSHOPTS` across Nix CLIs.

- Nix is now built using Meson

  As proposed in [RFC 132](https://github.com/NixOS/rfcs/pull/132), Nix's build system now uses Meson/Ninja. The old Make-based build system has been removed.

- Evaluation caching now works for dirty Git workdirs [#11992](https://github.com/NixOS/nix/pull/11992)

## Contributors

This release was made possible by the following 45 contributors:

- Anatoli Babenia [**(@abitrolly)**](https://github.com/abitrolly)
- Domagoj Mišković [**(@allrealmsoflife)**](https://github.com/allrealmsoflife)
- Yaroslav Bolyukin [**(@CertainLach)**](https://github.com/CertainLach)
- bryango [**(@bryango)**](https://github.com/bryango)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
- Matej Urbas [**(@mupdt)**](https://github.com/mupdt)
- elikoga [**(@elikoga)**](https://github.com/elikoga)
- wh0 [**(@wh0)**](https://github.com/wh0)
- Félix [**(@picnoir)**](https://github.com/picnoir)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- Gavin John [**(@Pandapip1)**](https://github.com/Pandapip1)
- Travis A. Everett [**(@abathur)**](https://github.com/abathur)
- Vladimir Panteleev [**(@CyberShadow)**](https://github.com/CyberShadow)
- Ilja [**(@suruaku)**](https://github.com/suruaku)
- Jason Yundt [**(@Jayman2000)**](https://github.com/Jayman2000)
- Mike Kusold [**(@kusold)**](https://github.com/kusold)
- Andy Hamon [**(@andrewhamon)**](https://github.com/andrewhamon)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- Greg Curtis [**(@gcurtis)**](https://github.com/gcurtis)
- Andrew Poelstra [**(@apoelstra)**](https://github.com/apoelstra)
- Linus Heckemann [**(@lheckemann)**](https://github.com/lheckemann)
- Tristan Ross [**(@RossComputerGuy)**](https://github.com/RossComputerGuy)
- Dominique Martinet [**(@martinetd)**](https://github.com/martinetd)
- h0nIg [**(@h0nIg)**](https://github.com/h0nIg)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Shahar "Dawn" Or [**(@mightyiam)**](https://github.com/mightyiam)
- NAHO [**(@trueNAHO)**](https://github.com/trueNAHO)
- Ryan Hendrickson [**(@rhendric)**](https://github.com/rhendric)
- the-sun-will-rise-tomorrow [**(@the-sun-will-rise-tomorrow)**](https://github.com/the-sun-will-rise-tomorrow)
- Connor Baker [**(@ConnorBaker)**](https://github.com/ConnorBaker)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Jack Wilsdon [**(@jackwilsdon)**](https://github.com/jackwilsdon)
- Martin Häcker [**(@dwt)**](https://github.com/dwt)
- Martin Fischer [**(@not-my-profile)**](https://github.com/not-my-profile)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Graham Christensen [**(@grahamc)**](https://github.com/grahamc)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Siddarth Kumar [**(@siddarthkay)**](https://github.com/siddarthkay)
- Sergei Trofimovich [**(@trofi)**](https://github.com/trofi)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Mutsuha Asada [**(@momeemt)**](https://github.com/momeemt)
- Parker Jones [**(@knotapun)**](https://github.com/knotapun)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- dbdr [**(@dbdr)**](https://github.com/dbdr)
- myclevorname [**(@myclevorname)**](https://github.com/myclevorname)
- Philipp Otterbein
