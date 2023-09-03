# Installing a Binary Distribution

The easiest way to install Nix is to run the following command:

```console
$ curl -L https://nixos.org/nix/install | sh
```

This will run the installer interactively (causing it to explain what
it is doing more explicitly), and perform the default "type" of install
for your platform:
- single-user on Linux
- multi-user on macOS

  > **Notes on read-only filesystem root in macOS 10.15 Catalina +**
  >
  > - It took some time to support this cleanly. You may see posts,
  >   examples, and tutorials using obsolete workarounds.
  > - Supporting it cleanly made macOS installs too complex to qualify
  >   as single-user, so this type is no longer supported on macOS.

We recommend the multi-user install if it supports your platform and
you can authenticate with `sudo`.

# Single User Installation

To explicitly select a single-user installation on your system:

```console
$ curl -L https://nixos.org/nix/install | sh -s -- --no-daemon
```

This will perform a single-user installation of Nix, meaning that `/nix`
is owned by the invoking user. You can run this under your usual user
account or root. The script will invoke `sudo` to create `/nix`
if it doesn’t already exist. If you don’t have `sudo`, you should
manually create `/nix` first as root, e.g.:

```console
$ mkdir /nix
$ chown alice /nix
```

The install script will modify the first writable file from amongst
`.bash_profile`, `.bash_login` and `.profile` to source
`~/.nix-profile/etc/profile.d/nix.sh`. You can set the
`NIX_INSTALLER_NO_MODIFY_PROFILE` environment variable before executing
the install script to disable this behaviour.

# Multi User Installation

The multi-user Nix installation creates system users, and a system
service for the Nix daemon.

**Supported Systems**
- Linux running systemd, with SELinux disabled
- macOS

You can instruct the installer to perform a multi-user installation on
your system:

```console
$ curl -L https://nixos.org/nix/install | sh -s -- --daemon
```

The multi-user installation of Nix will create build users between the
user IDs 30001 and 30032, and a group with the group ID 30000. You
can run this under your usual user account or root. The script
will invoke `sudo` as needed.

> **Note**
>
> If you need Nix to use a different group ID or user ID set, you will
> have to download the tarball manually and [edit the install
> script](#installing-from-a-binary-tarball).

The installer will modify `/etc/bashrc`, and `/etc/zshrc` if they exist.
The installer will first back up these files with a `.backup-before-nix`
extension. The installer will also create `/etc/profile.d/nix.sh`.

# macOS Installation

[]{#sect-macos-installation-change-store-prefix}[]{#sect-macos-installation-encrypted-volume}[]{#sect-macos-installation-symlink}[]{#sect-macos-installation-recommended-notes}
<!-- Note: anchors above to catch permalinks to old explanations -->

We believe we have ironed out how to cleanly support the read-only root
on modern macOS. New installs will do this automatically.

This section previously detailed the situation, options, and trade-offs,
but it now only outlines what the installer does. You don't need to know
this to run the installer, but it may help if you run into trouble:

- create a new APFS volume for your Nix store
- update `/etc/synthetic.conf` to direct macOS to create a "synthetic"
  empty root directory to mount your volume
- specify mount options for the volume in `/etc/fstab`
  - `rw`: read-write
  - `noauto`: prevent the system from auto-mounting the volume (so the
    LaunchDaemon mentioned below can control mounting it, and to avoid
    masking problems with that mounting service).
  - `nobrowse`: prevent the Nix Store volume from showing up on your
    desktop; also keeps Spotlight from spending resources to index
    this volume
  <!-- TODO:
  - `suid`: honor setuid? surely not? ...
  - `owners`: honor file ownership on the volume

    For now I'll avoid pretending to understand suid/owners more
    than I do. There've been some vague reports of file-ownership
    and permission issues, particularly in cloud/VM/headless setups.
    My pet theory is that this has something to do with these setups
    not having a token that gets delegated to initial/admin accounts
    on macOS. See scripts/create-darwin-volume.sh for a little more.

    In any case, by Dec 4 2021, it _seems_ like some combination of
    suid, owners, and calling diskutil enableOwnership have stopped
    new reports from coming in. But I hesitate to celebrate because we
    haven't really named and catalogued the behavior, understood what
    we're fixing, and validated that all 3 components are essential.
  -->
- if you have FileVault enabled
    - generate an encryption password
    - put it in your system Keychain
    - use it to encrypt the volume
- create a system LaunchDaemon to mount this volume early enough in the
  boot process to avoid problems loading or restoring any programs that
  need access to your Nix store

# Installing a pinned Nix version from a URL

Version-specific installation URLs for all Nix versions
since 1.11.16 can be found at [releases.nixos.org](https://releases.nixos.org/?prefix=nix/).
The corresponding SHA-256 hash can be found in the directory for the given version.

These install scripts can be used the same as usual:

```console
$ curl -L https://releases.nixos.org/nix/nix-<version>/install | sh
```

# Installing from a binary tarball

You can also download a binary tarball that contains Nix and all its
dependencies. (This is what the install script at
<https://nixos.org/nix/install> does automatically.) You should unpack
it somewhere (e.g. in `/tmp`), and then run the script named `install`
inside the binary tarball:

```console
$ cd /tmp
$ tar xfj nix-1.8-x86_64-darwin.tar.bz2
$ cd nix-1.8-x86_64-darwin
$ ./install
```

If you need to edit the multi-user installation script to use different
group ID or a different user ID range, modify the variables set in the
file named `install-multi-user`.
