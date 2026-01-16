# Nix32 Encoding

Nix32 is Nix's variant of base-32 encoding, used for [store path digests](@docroot@/protocols/store-path.md), hash output via [`nix hash`](@docroot@/command-ref/new-cli/nix3-hash.md), and the [`outputHash`](@docroot@/language/advanced-attributes.md#adv-attr-outputHash) derivation attribute.

## Alphabet

The Nix32 alphabet consists of these 32 characters:

```
0 1 2 3 4 5 6 7 8 9 a b c d f g h i j k l m n p q r s v w x y z
```

The letters `e`, `o`, `u`, and `t` are omitted.

## Byte Order

Nix32 encoding processes the hash bytes from the end (last byte first), while base-16 encoding processes from the beginning (first byte first).

Consequently, the string sort order is determined primarily by the first bytes for base-16, and by the last bytes for Nix32.
