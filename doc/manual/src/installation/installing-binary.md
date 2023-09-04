# Installing a Binary Distribution

The easiest way to install Nix is to run the following command:

```console
$ sh <(curl -L https://nixos.org/nix/install)
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
$ sh <(curl -L https://nixos.org/nix/install) --no-daemon
```

This will perform a single-user installation of Nix, meaning that `/nix`
is owned by the invoking user. You should run this under your usual user
account, *not* as root. The script will invoke `sudo` to create `/nix`
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

You can uninstall Nix simply by running:

```console
$ rm -rf /nix
```

# Multi User Installation

The multi-user Nix installation creates system users, and a system
service for the Nix daemon.

**Supported Systems**
- Linux running systemd, with SELinux disabled
- macOS

You can instruct the installer to perform a multi-user installation on
your system:

```console
$ sh <(curl -L https://nixos.org/nix/install) --daemon
```

The multi-user installation of Nix will create build users between the
user IDs 30001 and 30032, and a group with the group ID 30000. You
should run this under your usual user account, *not* as root. The script
will invoke `sudo` as needed.

> **Note**
> 
> If you need Nix to use a different group ID or user ID set, you will
> have to download the tarball manually and [edit the install
> script](#installing-from-a-binary-tarball).

The installer will modify `/etc/bashrc`, and `/etc/zshrc` if they exist.
The installer will first back up these files with a `.backup-before-nix`
extension. The installer will also create `/etc/profile.d/nix.sh`.

You can uninstall Nix with the following commands:

```console
sudo rm -rf /etc/profile/nix.sh /etc/nix /nix ~root/.nix-profile ~root/.nix-defexpr ~root/.nix-channels ~/.nix-profile ~/.nix-defexpr ~/.nix-channels

# If you are on Linux with systemd, you will need to run:
sudo systemctl stop nix-daemon.socket
sudo systemctl stop nix-daemon.service
sudo systemctl disable nix-daemon.socket
sudo systemctl disable nix-daemon.service
sudo systemctl daemon-reload

# If you are on macOS, you will need to run:
sudo launchctl unload /Library/LaunchDaemons/org.nixos.nix-daemon.plist
sudo rm /Library/LaunchDaemons/org.nixos.nix-daemon.plist
```

There may also be references to Nix in `/etc/profile`, `/etc/bashrc`,
and `/etc/zshrc` which you may remove.

# macOS Installation <a name="sect-macos-installation-change-store-prefix"></a><a name="sect-macos-installation-encrypted-volume"></a><a name="sect-macos-installation-symlink"></a><a name="sect-macos-installation-recommended-notes"></a>
<!-- Note: anchors above to catch permalinks to old explanations -->

We believe we have ironed out how to cleanly support the read-only root
on modern macOS. New installs will do this automatically, and you can
also re-run a new installer to convert your existing setup.

This section previously detailed the situation, options, and trade-offs,
but it now only outlines what the installer does. You don't need to know
this to run the installer, but it may help if you run into trouble:

- create a new APFS volume for your Nix store
- update `/etc/synthetic.conf` to direct macOS to create a "synthetic"
  empty root directory to mount your volume
- specify mount options for the volume in `/etc/fstab`
- if you have FileVault enabled
    - generate an encryption password
    - put it in your system Keychain
    - use it to encrypt the volume
- create a system LaunchDaemon to mount this volume early enough in the
  boot process to avoid problems loading or restoring any programs that
  need access to your Nix store

# Installing a pinned Nix version from a URL

NixOS.org hosts version-specific installation URLs for all Nix versions
since 1.11.16, at `https://releases.nixos.org/nix/nix-version/install`.

These install scripts can be used the same as the main NixOS.org
installation script:

```console
$ sh <(curl -L https://nixos.org/nix/install)
```

In the same directory of the install script are sha256 sums, and gpg
signature files.

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
