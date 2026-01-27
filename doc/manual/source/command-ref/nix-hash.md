# Name

`nix-hash` - compute the cryptographic hash of a path

# Synopsis

`nix-hash` [`--flat`] [`--base32`] [`--truncate`] [`--type` *hashAlgo*] *path…*

`nix-hash` [`--to-base16`|`--to-base32`|`--to-base64`|`--to-sri`] [`--type` *hashAlgo*] *hash…*

# Description

The command `nix-hash` computes the cryptographic hash of the contents
of each *path* and prints it on standard output. By default, it computes
an MD5 hash, but other hash algorithms are available as well. The hash
is printed in hexadecimal. To generate the same hash as
`nix-prefetch-url` you have to specify multiple arguments, see below for
an example.

The hash is computed over a *serialisation* of each path: a dump of
the file system tree rooted at the path. This allows directories and
symlinks to be hashed as well as regular files. The dump is in the
*[Nix Archive (NAR)][Nix Archive] format* produced by [`nix-store
--dump`](@docroot@/command-ref/nix-store/dump.md).  Thus, `nix-hash path`
yields the same cryptographic hash as `nix-store --dump path |
md5sum`.

[Nix Archive]: @docroot@/store/file-system-object/content-address.md#serial-nix-archive

# Options

- `--flat`

  Print the cryptographic hash of the contents of each regular file *path*.
  That is, instead of computing
  the hash of the [Nix Archive (NAR)](@docroot@/store/file-system-object/content-address.md#serial-nix-archive) of *path*,
  just [directly hash](@docroot@/store/file-system-object/content-address.md#serial-flat) *path* as is.
  This requires *path* to resolve to a regular file rather than directory.
  The result is identical to that produced by the GNU commands
  `md5sum` and `sha1sum`.

- `--base16`

  Print the hash in a hexadecimal representation (default).

- `--base32`

  Print the hash in a base-32 representation rather than hexadecimal.
  This base-32 representation is more compact and can be used in Nix
  expressions (such as in calls to `fetchurl`).

- `--base64`

  Similar to --base32, but print the hash in a base-64 representation,
  which is more compact than the base-32 one.

- `--sri`

  Print the hash in SRI format with base-64 encoding.
  The type of hash algorithm will be prepended to the hash string,
  followed by a hyphen (-) and the base-64 hash body.

- `--truncate`

  Truncate hashes longer than 160 bits (such as SHA-256) to 160 bits.

- `--type` *hashAlgo*

  Use the specified cryptographic hash algorithm, which can be one of
  `blake3`, `md5`, `sha1`, `sha256`, and `sha512`.

- `--to-base16`

  Don’t hash anything, but convert the base-32 hash representation
  *hash* to hexadecimal.

- `--to-base32`

  Don’t hash anything, but convert the hexadecimal hash representation
  *hash* to base-32.

- `--to-base64`

  Don’t hash anything, but convert the hexadecimal hash representation
  *hash* to base-64.

- `--to-sri`

  Don’t hash anything, but convert the hexadecimal hash representation
  *hash* to SRI.

# Examples

Computing the same hash as `nix-prefetch-url`:

```console
$ nix-prefetch-url file://<(echo test)
1lkgqb6fclns49861dwk9rzb6xnfkxbpws74mxnx01z9qyv1pjpj
$ nix-hash --type sha256 --flat --base32 <(echo test)
1lkgqb6fclns49861dwk9rzb6xnfkxbpws74mxnx01z9qyv1pjpj
```

Computing hashes:

```console
$ mkdir test
$ echo "hello" > test/world

$ nix-hash test/ (MD5 hash; default)
8179d3caeff1869b5ba1744e5a245c04

$ nix-store --dump test/ | md5sum (for comparison)
8179d3caeff1869b5ba1744e5a245c04  -

$ nix-hash --type sha1 test/
e4fd8ba5f7bbeaea5ace89fe10255536cd60dab6

$ nix-hash --type sha1 --base16 test/
e4fd8ba5f7bbeaea5ace89fe10255536cd60dab6

$ nix-hash --type sha1 --base32 test/
nvd61k9nalji1zl9rrdfmsmvyyjqpzg4

$ nix-hash --type sha1 --base64 test/
5P2Lpfe76upazon+ECVVNs1g2rY=

$ nix-hash --type sha1 --sri test/
sha1-5P2Lpfe76upazon+ECVVNs1g2rY=

$ nix-hash --type sha256 --flat test/
error: reading file `test/': Is a directory

$ nix-hash --type sha256 --flat test/world
5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03
```

Converting between hexadecimal, base-32, base-64, and SRI:

```console
$ nix-hash --type sha1 --to-base32 e4fd8ba5f7bbeaea5ace89fe10255536cd60dab6
nvd61k9nalji1zl9rrdfmsmvyyjqpzg4

$ nix-hash --type sha1 --to-base16 nvd61k9nalji1zl9rrdfmsmvyyjqpzg4
e4fd8ba5f7bbeaea5ace89fe10255536cd60dab6

$ nix-hash --type sha1 --to-base64 e4fd8ba5f7bbeaea5ace89fe10255536cd60dab6
5P2Lpfe76upazon+ECVVNs1g2rY=

$ nix-hash --type sha1 --to-sri nvd61k9nalji1zl9rrdfmsmvyyjqpzg4
sha1-5P2Lpfe76upazon+ECVVNs1g2rY=

$ nix-hash --to-base16 sha1-5P2Lpfe76upazon+ECVVNs1g2rY=
e4fd8ba5f7bbeaea5ace89fe10255536cd60dab6
```
