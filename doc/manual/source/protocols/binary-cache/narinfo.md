# `.narinfo` Format

A `.narinfo` file contains the [metadata of a store object](@docroot@/store/store-object.md#metadata) in the [binary cache](@docroot@/protocols/binary-cache/index.md) format.
It is a simple line-oriented format where each line is a `Key: Value` pair.
Some keys (e.g. `Sig`) may appear multiple times.

The file is named `<hash>.narinfo`, where `<hash>` is the [hash part](@docroot@/store/store-path.md#digest) of the store object's [store path](@docroot@/store/store-path.md).

The fields correspond to those documented in the [store object info](@docroot@/protocols/json/store-object-info.md) JSON format:

| `.narinfo` field | JSON field | Differences |
|---|---|---|
| `StorePath` | [`path`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_path) | Full [store path](@docroot@/store/store-path.md) rather than [store path base name](@docroot@/store/store-path.md#base-name) |
| `URL` | [`url`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_url) | |
| `Compression` | [`compression`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_compression) | Defaults to `bzip2` if omitted |
| `FileHash` | [`downloadHash`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_downloadHash) | String-encoded hash rather than structured |
| `FileSize` | [`downloadSize`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_downloadSize) | |
| `NarHash` | [`narHash`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_narHash) | String-encoded hash rather than structured |
| `NarSize` | [`narSize`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_narSize) | |
| `References` | [`references`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_references) | Space-separated [store path base names](@docroot@/store/store-path.md#base-name) rather than a JSON array |
| `Deriver` | [`deriver`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_deriver) | [Store path base name](@docroot@/store/store-path.md#base-name); `unknown-deriver` instead of `null` |
| `Sig` | [`signatures`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_signatures) | May appear multiple times rather than using an array |
| `CA` | [`ca`](@docroot@/protocols/json/store-object-info.md#oneOf_i2_ca) | String-encoded [content address](@docroot@/store/store-object/content-address.md) rather than structured |

## Example

<!-- TODO make this include a test file instead of being manually written once we have one -->

```
StorePath: /nix/store/n5wkd9frr45pa74if5gpz9j7mifg27fh-foo
URL: nar/1w1fff338fvdw53sqgamddn1b2xgds473pv6y13gizdbqjv4i5p3.nar.xz?sha256=1w1fff338fvdw53sqgamddn1b2xgds473pv6y13gizdbqjv4i5p3
Compression: xz
FileHash: sha256:09ymwqf5i9q7d4dm7x4pjjcqqj0qrcp5lnznbh42gfsci5hcbqqm
FileSize: 4029176
NarHash: sha256:09ymwqf5i9q7d4dm7x4pjjcqqj0qrcp5lnznbh42gfsci5hcbqqm
NarSize: 34878
References: g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar n5wkd9frr45pa74if5gpz9j7mifg27fh-foo
Deriver: g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv
Sig: asdf:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==
Sig: qwer:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==
CA: fixed:r:sha256:1lr187v6dck1rjh2j6svpikcfz53wyl3qrlcbb405zlh13x0khhh
```
