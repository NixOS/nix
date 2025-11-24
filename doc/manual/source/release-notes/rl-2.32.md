# Release 2.32.0 (2025-10-06)

## Incompatible changes

- Removed support for daemons and clients older than Nix 2.0 [#13951](https://github.com/NixOS/nix/pull/13951)

  We have dropped support in the daemon worker protocol for daemons and clients that don't speak at least version 18 of the protocol. This first Nix release that supports this version is Nix 2.0, released in February 2018.

- Derivation JSON format now uses store path basenames only [#13570](https://github.com/NixOS/nix/issues/13570) [#13980](https://github.com/NixOS/nix/pull/13980)

  Experience with many JSON frameworks (e.g. nlohmann/json in C++, Serde in Rust, and Aeson in Haskell) has shown that the use of the store directory in JSON formats is an impediment to systematic JSON formats, because it requires the serializer/deserializer to take an extra paramater (the store directory).

  We ultimately want to rectify this issue with all JSON formats to the extent allowed by our stability promises. To start with, we are changing the JSON format for derivations because the `nix derivation` commands are — in addition to being formally unstable — less widely used than other unstable commands.

  See the documentation on the [JSON format for derivations](@docroot@/protocols/json/derivation/index.md) for further details.

- C API: `nix_get_attr_name_byidx`, `nix_get_attr_byidx` take a `nix_value *` instead of `const nix_value *` [#13987](https://github.com/NixOS/nix/pull/13987)

  In order to accommodate a more optimized internal representation of attribute set merges these functions require
  a mutable `nix_value *` that might be modified on access. This does *not* break the ABI of these functions.

## New features

- C API: Add lazy attribute and list item accessors [#14030](https://github.com/NixOS/nix/pull/14030)

  The C API now includes lazy accessor functions for retrieving values from lists and attribute sets without forcing evaluation:

  - `nix_get_list_byidx_lazy()` - Get a list element without forcing its evaluation
  - `nix_get_attr_byname_lazy()` - Get an attribute value by name without forcing evaluation
  - `nix_get_attr_byidx_lazy()` - Get an attribute by index without forcing evaluation

  These functions are useful when forwarding unevaluated sub-values to other lists, attribute sets, or function calls. They allow more efficient handling of Nix values by deferring evaluation until actually needed.

  Additionally, bounds checking has been improved for all `_byidx` functions to properly validate indices before access, preventing potential out-of-bounds errors.

  The documentation for `NIX_ERR_KEY` error handling has also been clarified to specify when this error code is returned.

- HTTP binary caches now support transparent compression for metadata

  HTTP binary cache stores can now compress `.narinfo`, `.ls`, and build log files before uploading them,
  reducing bandwidth usage and storage requirements. The compression is applied transparently using the
  `Content-Encoding` header, allowing compatible clients to automatically decompress the files.

  Three new configuration options control this behavior:
  - `narinfo-compression`: Compression method for `.narinfo` files
  - `ls-compression`: Compression method for `.ls` files
  - `log-compression`: Compression method for build logs in `log/` directory

  Example usage:
  ```
  nix copy --to 'http://cache.example.com?narinfo-compression=gzip&ls-compression=gzip' /nix/store/...
  nix store copy-log --to 'http://cache.example.com?log-compression=br' /nix/store/...
  ```

- Temporary build directories no longer include derivation names [#13839](https://github.com/NixOS/nix/pull/13839)

  Temporary build directories created during derivation builds no longer include the derivation name in their path to avoid build failures when the derivation name is too long. This change ensures predictable prefix lengths for build directories under `/nix/var/nix/builds`.

- External derivation builders [#14145](https://github.com/NixOS/nix/pull/14145)

  These are helper programs that Nix calls to perform derivations for specified system types, e.g. by using QEMU to emulate a different type of platform. For more information, see the [`external-builders` setting](../command-ref/conf-file.md#conf-external-builders).

  This is currently an experimental feature.

## Performance improvements

- Optimize memory usage of attribute set merges [#13987](https://github.com/NixOS/nix/pull/13987)

  [Attribute set update operations](@docroot@/language/operators.md#update) have been optimized to
  reduce reallocations in cases when the second operand is small.

  For typical evaluations of nixpkgs this optimization leads to ~20% less memory allocated in total
  without significantly affecting evaluation performance.

  See [eval-attrset-update-layer-rhs-threshold](@docroot@/command-ref/conf-file.md#conf-eval-attrset-update-layer-rhs-threshold)

- Substituted flake inputs are no longer re-copied to the store [#14041](https://github.com/NixOS/nix/pull/14041)

  Since 2.25, Nix would fail to store a cache entry for substituted flake inputs, which in turn would cause them to be re-copied to the store on initial evaluation. Caching these inputs results in a near doubling of performance in some cases — especially on I/O-bound machines and when using commands that fetch many inputs, like `nix flake [archive|prefetch-inputs]`.

- `nix flake check` now skips derivations that can be substituted [#13574](https://github.com/NixOS/nix/pull/13574)

  Previously, `nix flake check` would evaluate and build/substitute all
  derivations. Now, it will skip downloading derivations that can be substituted.
  This can drastically decrease the time invocations take in environments where
  checks may already be cached (like in CI).

- `fetchTarball` and `fetchurl` now correctly substitute (#14138)

  At some point we stopped substituting calls to `fetchTarball` and `fetchurl` with a set `narHash` to avoid incorrectly substituting things in `fetchTree`, even though it would be safe to substitute when calling the legacy `fetch{Tarball,url}`. This fixes that regression where it is safe.
- Started moving AST allocations into a bump allocator [#14088](https://github.com/NixOS/nix/issues/14088)

  This leaves smaller, immutable structures in the AST. So far this saves about 2% memory on a NixOS config evaluation.
## Contributors

This release was made possible by the following 32 contributors:

- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- dram [**(@dramforever)**](https://github.com/dramforever)
- Ephraim Siegfried [**(@EphraimSiegfried)**](https://github.com/EphraimSiegfried)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Taeer Bar-Yam [**(@Radvendii)**](https://github.com/Radvendii)
- Emily [**(@emilazy)**](https://github.com/emilazy)
- Jens Petersen [**(@juhp)**](https://github.com/juhp)
- Bernardo Meurer [**(@lovesegfault)**](https://github.com/lovesegfault)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Leandro Emmanuel Reina Kiperman [**(@kip93)**](https://github.com/kip93)
- Marie [**(@NyCodeGHG)**](https://github.com/NyCodeGHG)
- Ethan Evans [**(@ethanavatar)**](https://github.com/ethanavatar)
- Yaroslav Bolyukin [**(@CertainLach)**](https://github.com/CertainLach)
- Matej Urbas [**(@urbas)**](https://github.com/urbas)
- Jami Kettunen [**(@JamiKettunen)**](https://github.com/JamiKettunen)
- Clayton [**(@netadr)**](https://github.com/netadr)
- Grégory Marti [**(@gmarti)**](https://github.com/gmarti)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- rszyma [**(@rszyma)**](https://github.com/rszyma)
- Philip Wilk [**(@philipwilk)**](https://github.com/philipwilk)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Tom Westerhout [**(@twesterhout)**](https://github.com/twesterhout)
- Tristan Ross [**(@RossComputerGuy)**](https://github.com/RossComputerGuy)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Jean-François Roche [**(@jfroche)**](https://github.com/jfroche)
- Seth Flynn [**(@getchoo)**](https://github.com/getchoo)
- éclairevoyant [**(@eclairevoyant)**](https://github.com/eclairevoyant)
- Glen Huang [**(@hgl)**](https://github.com/hgl)
- osman - オスマン [**(@osbm)**](https://github.com/osbm)
- David McFarland [**(@corngood)**](https://github.com/corngood)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Sinan Mohd [**(@sinanmohd)**](https://github.com/sinanmohd)
- Philipp Otterbein
