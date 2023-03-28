# Installing a pinned Nix version from a URL

NixOS.org hosts version-specific installation URLs for all Nix versions
since version 1.11.16 (see all available versions at [releases.nixos.org](https://releases.nixos.org/) under `nix/`).

The general format of the URLs pointing to installers of specific Nix versions is `https://releases.nixos.org/nix/nix-<VERSION>/install`. For example:
+ https://releases.nixos.org/nix/nix-1.11.16/install
+ https://releases.nixos.org/nix/nix-2.13.2/install

These install scripts can be used the same way as the main [NixOS.org
installation script](https://nixos.org/download.html):

```console
$ curl -L https://releases.nixos.org/nix/nix-1.11.16/install | sh

$ curl -L https://releases.nixos.org/nix/nix-2.13.2/install  | sh
```

In the same directory of the install script are SHA-256 checksums and GPG
signature files for verification.

