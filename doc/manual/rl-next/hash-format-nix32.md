synopsis: Rename hash format `base32` to `nix32`
prs: #9452
description: {

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