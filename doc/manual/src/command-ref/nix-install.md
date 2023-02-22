# Name

`install` - The Nix installer

# Synopsis

`install` {`--daemon` | `--no-daemon` | `--yes` | `--no-channel-add` | `--no-modify-profile` | `-daemon-user-count` [*number*] | `--nix-extra-conf-file` | `--tarball-url-prefix` [???] }

# Description

> PR_COMMENT This description is sloppy by choice; don't want to spend too much time on it if this page is rejected.

1. **`nix-install.sh`** (fictitious name!)

   The shell script at https://nixos.org/nix/install (let's call it **`nix-install.sh`**) is built from [`scripts/install.in`](https://github.com/NixOS/nix/blob/924ef6761bbbc75fda3cf85dc1c8d782130291b4/scripts/install.in).

   It provides the following options:

   + `--tarball-url-prefix`

     From a comment: `Use this command-line option to fetch the tarballs using nar-serve or Cachix`

     > PR_COMMENT no clue how to use this

2. **[`scripts/install-nix-from-closure.sh`][2]**

   `nix-install.sh` will download the latest Nix release, unpack the tarball, and it seems that `install-nix-from-closure.sh` will be called at one point. It provides the following options:

    + `--daemon`

      Installs and configures a background daemon that manages the store, providing multi-user support and better isolation for local builds. Both for security and reproducibility, this method is recommended if supported on your platform.

      See https://nixos.org/manual/nix/stable/installation/installing-binary.html#multi-user-installation

    + `--no-daemon`

      Simple, single-user installation that does not require root and is trivial to uninstall. (default)

    + `--yes`

      Run the script non-interactively, accepting all prompts.

    + `--no-channel-add`

      Don't add any channels. `nixpkgs-unstable` is installed by default.

    + `--no-modify-profile`

      Don't modify the user profile to automatically load nix.

    + `--daemon-user-count`

      Number of build users to create. Defaults to 32, if this option is not provided.

    + `--nix-extra-conf-file`

      Path to nix.conf to prepend when installing `/etc/nix/nix.conf`

> PR_COMMENT Things to note here:
> + The install script is **not** provided by Nix (the application) but by the Nix project as a convencience to deploy Nix application (hence it will never show up in the Nix commands; the reason why I put this under Utilities.)
> + It is used indirectly most of the time via `curl`, `wget`, etc.
> + Building the Nix project repo will yield an install script that **can** be used directly. (TODO: Expand the "Build from source" sections)
> + Some aspects of the install script cannot be modified using options / flags, but only by editing the its source (as noted at several places in the Installation section). Corollary: Document that it is a two-stage process.
