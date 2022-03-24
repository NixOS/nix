R""(

# Examples

* Create a content-addressed representation of the closure of GNU Hello:

  ```console
  # nix store make-content-addressed nixpkgs#hello
  …
  rewrote '/nix/store/v5sv61sszx301i0x6xysaqzla09nksnd-hello-2.10' to '/nix/store/5skmmcb9svys5lj3kbsrjg7vf2irid63-hello-2.10'
  ```

  Since the resulting paths are content-addressed, they are always
  trusted and don't need signatures to copied to another store:

  ```console
  # nix copy --to /tmp/nix --trusted-public-keys '' /nix/store/5skmmcb9svys5lj3kbsrjg7vf2irid63-hello-2.10
  ```

  By contrast, the original closure is input-addressed, so it does
  need signatures to be trusted:

  ```console
  # nix copy --to /tmp/nix --trusted-public-keys '' nixpkgs#hello
  cannot add path '/nix/store/zy9wbxwcygrwnh8n2w9qbbcr6zk87m26-libunistring-0.9.10' because it lacks a valid signature
  ```

* Create a content-addressed representation of the current NixOS
  system closure:

  ```console
  # nix store make-content-addressed /run/current-system
  ```

# Description

This command converts the closure of the store paths specified by
*installables* to content-addressed form. Nix store paths are usually
*input-addressed*, meaning that the hash part of the store path is
computed from the contents of the derivation (i.e., the build-time
dependency graph). Input-addressed paths need to be signed by a
trusted key if you want to import them into a store, because we need
to trust that the contents of the path were actually built by the
derivation.

By contrast, in a *content-addressed* path, the hash part is computed
from the contents of the path. This allows the contents of the path to
be verified without any additional information such as
signatures. This means that a command like

```console
# nix store build /nix/store/5skmmcb9svys5lj3kbsrjg7vf2irid63-hello-2.10 \
    --substituters https://my-cache.example.org
```

will succeed even if the binary cache `https://my-cache.example.org`
doesn't present any signatures.

)""
