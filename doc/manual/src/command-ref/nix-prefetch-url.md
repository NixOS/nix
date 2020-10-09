# Name

`nix-prefetch-url` - copy a file from a URL into the store and print its hash

# Synopsis

`nix-prefetch-url` *url* [*hash*]
  [`--type` *hashAlgo*]
  [`--print-path`]
  [`--unpack`]
  [`--name` *name*]

# Description

The command `nix-prefetch-url` downloads the file referenced by the URL
*url*, prints its cryptographic hash, and copies it into the Nix store.
The file name in the store is `hash-baseName`, where *baseName* is
everything following the final slash in *url*.

This command is just a convenience for Nix expression writers. Often a
Nix expression fetches some source distribution from the network using
the `fetchurl` expression contained in Nixpkgs. However, `fetchurl`
requires a cryptographic hash. If you don't know the hash, you would
have to download the file first, and then `fetchurl` would download it
again when you build your Nix expression. Since `fetchurl` uses the same
name for the downloaded file as `nix-prefetch-url`, the redundant
download can be avoided.

If *hash* is specified, then a download is not performed if the Nix
store already contains a file with the same hash and base name.
Otherwise, the file is downloaded, and an error is signaled if the
actual hash of the file does not match the specified hash.

This command prints the hash on standard output. Additionally, if the
option `--print-path` is used, the path of the downloaded file in the
Nix store is also printed.

# Options

  - `--type` *hashAlgo*  
    Use the specified cryptographic hash algorithm, which can be one of
    `md5`, `sha1`, and `sha256`.

  - `--print-path`  
    Print the store path of the downloaded file on standard output.

  - `--unpack`  
    Unpack the archive (which must be a tarball or zip file) and add the
    result to the Nix store. The resulting hash can be used with
    functions such as Nixpkgs’s `fetchzip` or `fetchFromGitHub`.

  - `--executable`  
    Set the executable bit on the downloaded file.

  - `--name` *name*  
    Override the name of the file in the Nix store. By default, this is
    `hash-basename`, where *basename* is the last component of *url*.
    Overriding the name is necessary when *basename* contains characters
    that are not allowed in Nix store paths.

# Examples

```console
$ nix-prefetch-url ftp://ftp.gnu.org/pub/gnu/hello/hello-2.10.tar.gz
0ssi1wpaf7plaswqqjwigppsg5fyh99vdlb9kzl7c9lng89ndq1i
```

```console
$ nix-prefetch-url --print-path mirror://gnu/hello/hello-2.10.tar.gz
0ssi1wpaf7plaswqqjwigppsg5fyh99vdlb9kzl7c9lng89ndq1i
/nix/store/3x7dwzq014bblazs7kq20p9hyzz0qh8g-hello-2.10.tar.gz
```

```console
$ nix-prefetch-url --unpack --print-path https://github.com/NixOS/patchelf/archive/0.8.tar.gz
079agjlv0hrv7fxnx9ngipx14gyncbkllxrp9cccnh3a50fxcmy7
/nix/store/19zrmhm3m40xxaw81c8cqm6aljgrnwj2-0.8.tar.gz
```
