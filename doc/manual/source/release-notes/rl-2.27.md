# Release 2.27.0 (2025-03-03)

- `inputs.self.submodules` flake attribute [#12421](https://github.com/NixOS/nix/pull/12421)

  Flakes in Git repositories can now declare that they need Git submodules to be enabled:
  ```
  {
    inputs.self.submodules = true;
  }
  ```
  Thus, it's no longer needed for the caller of the flake to pass `submodules = true`.

- Git LFS support [#10153](https://github.com/NixOS/nix/pull/10153) [#12468](https://github.com/NixOS/nix/pull/12468)

  The Git fetcher now supports Large File Storage (LFS). This can be enabled by passing the attribute `lfs = true` to the fetcher, e.g.
  ```console
  nix flake prefetch 'git+ssh://git@github.com/Apress/repo-with-large-file-storage.git?lfs=1'
  ```

  A flake can also declare that it requires LFS to be enabled:
  ```
  {
    inputs.self.lfs = true;
  }
  ```

  Author: [**@b-camacho**](https://github.com/b-camacho), [**@kip93**](https://github.com/kip93)

- Handle the case where a chroot store is used and some inputs are in the "host" `/nix/store` [#12512](https://github.com/NixOS/nix/pull/12512)

  The evaluator now presents a "union" filesystem view of the `/nix/store` in the host and the chroot.

  This change also removes some hacks that broke `builtins.{path,filterSource}` in chroot stores [#11503](https://github.com/NixOS/nix/issues/11503).

- `nix flake prefetch` now has a `--out-link` option [#12443](https://github.com/NixOS/nix/pull/12443)

- Set `FD_CLOEXEC` on sockets created by curl [#12439](https://github.com/NixOS/nix/pull/12439)

  Curl created sockets without setting `FD_CLOEXEC`/`SOCK_CLOEXEC`. This could previously cause connections to remain open forever when using commands like `nix shell`. This change sets the `FD_CLOEXEC` flag using a `CURLOPT_SOCKOPTFUNCTION` callback.

- Add BLAKE3 hash algorithm [#12379](https://github.com/NixOS/nix/pull/12379)

  Nix now supports the BLAKE3 hash algorithm as an experimental feature (`blake3-hashes`):

  ```console
  # nix hash file ./file --type blake3 --extra-experimental-features blake3-hashes
  blake3-34P4p+iZXcbbyB1i4uoF7eWCGcZHjmaRn6Y7QdynLwU=
  ```

## Contributors

This release was made possible by the following 21 contributors:

- Aiden Fox Ivey [**(@aidenfoxivey)**](https://github.com/aidenfoxivey)
- Ben Millwood [**(@bmillwood)**](https://github.com/bmillwood)
- Brian Camacho [**(@b-camacho)**](https://github.com/b-camacho)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Fabian Möller [**(@B4dM4n)**](https://github.com/B4dM4n)
- Illia Bobyr [**(@ilya-bobyr)**](https://github.com/ilya-bobyr)
- Ivan Trubach [**(@tie)**](https://github.com/tie)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Leandro Emmanuel Reina Kiperman [**(@kip93)**](https://github.com/kip93)
- MaxHearnden [**(@MaxHearnden)**](https://github.com/MaxHearnden)
- Philipp Otterbein
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Sandro [**(@SuperSandro2000)**](https://github.com/SuperSandro2000)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Silvan Mosberger [**(@infinisil)**](https://github.com/infinisil)
- Someone [**(@SomeoneSerge)**](https://github.com/SomeoneSerge)
- Steve Walker [**(@stevalkr)**](https://github.com/stevalkr)
- bcamacho2 [**(@bcamacho2)**](https://github.com/bcamacho2)
- silvanshade [**(@silvanshade)**](https://github.com/silvanshade)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
