# Name

`nix-store --generate-binary-cache-key` - generate key pair to use for a binary cache

## Synopsis

`nix-store` `--generate-binary-cache-key` *key-name* *secret-key-file* *public-key-file*

## Description

This command generates an [Ed25519 key pair](http://ed25519.cr.yp.to/)
that can be used to create a signed binary cache. It takes three
mandatory parameters:

1.  A key name, such as `cache.example.org-1`, that is used to look up
    keys on the client when it verifies signatures. It can be anything,
    but it’s suggested to use the host name of your cache (e.g.
    `cache.example.org`) with a suffix denoting the number of the key
    (to be incremented every time you need to revoke a key).

2.  The file name where the secret key is to be stored.

3.  The file name where the public key is to be stored.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}
