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

## Uninstalling

### Linux

```console
sudo rm -rf /etc/profile/nix.sh /etc/nix /nix ~root/.nix-profile ~root/.nix-defexpr ~root/.nix-channels ~/.nix-profile ~/.nix-defexpr ~/.nix-channels

# If you are on Linux with systemd, you will need to run:
sudo systemctl stop nix-daemon.socket
sudo systemctl stop nix-daemon.service
sudo systemctl disable nix-daemon.socket
sudo systemctl disable nix-daemon.service
sudo systemctl daemon-reload
```

There may also be references to Nix in `/etc/profile`, `/etc/bashrc`,
and `/etc/zshrc` which you may remove.

### macOS

1. Edit `/etc/zshrc` and `/etc/bashrc` to remove the lines sourcing
   `nix-daemon.sh`, which should look like this:

   ```bash
   # Nix
   if [ -e '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh' ]; then
     . '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh'
   fi
   # End Nix
   ```

   If these files haven't been altered since installing Nix you can simply put
   the backups back in place:

   ```console
   sudo mv /etc/zshrc.backup-before-nix /etc/zshrc
   sudo mv /etc/bashrc.backup-before-nix /etc/bashrc
   ```

   This will stop shells from sourcing the file and bringing everything you
   installed using Nix in scope.

2. Stop and remove the Nix daemon services:

   ```console
   sudo launchctl unload /Library/LaunchDaemons/org.nixos.nix-daemon.plist
   sudo rm /Library/LaunchDaemons/org.nixos.nix-daemon.plist
   sudo launchctl unload /Library/LaunchDaemons/org.nixos.darwin-store.plist
   sudo rm /Library/LaunchDaemons/org.nixos.darwin-store.plist
   ```

   This stops the Nix daemon and prevents it from being started next time you
   boot the system.

3. Remove the `nixbld` group and the `_nixbuildN` users:

   ```console
   sudo dscl . -delete /Groups/nixbld
   for u in $(sudo dscl . -list /Users | grep _nixbld); do sudo dscl . -delete /Users/$u; done
   ```

   This will remove all the build users that no longer serve a purpose.

4. Edit fstab using `sudo vifs` to remove the line mounting the Nix Store
   volume on `/nix`, which looks like this,
   `LABEL=Nix\040Store /nix apfs rw,nobrowse`. This will prevent automatic
   mounting of the Nix Store volume.

5. Edit `/etc/synthetic.conf` to remove the `nix` line. If this is the only
   line in the file you can remove it entirely, `sudo rm /etc/synthetic.conf`.
   This will prevent the creation of the empty `/nix` directory to provide a
   mountpoint for the Nix Store volume.

6. Remove the files Nix added to your system:

   ```console
   sudo rm -rf /etc/nix /var/root/.nix-profile /var/root/.nix-defexpr /var/root/.nix-channels ~/.nix-profile ~/.nix-defexpr ~/.nix-channels
   ```

   This gets rid of any data Nix may have created except for the store which is
   removed next.

7. Remove the Nix Store volume:
   
   ```console
   sudo diskutil apfs deleteVolume /nix
   ```

   This will remove the Nix Store volume and everything that was added to the
   store.

> **Note**
> 
> After you complete the steps here, you will still have an empty `/nix`
> directory. This is an expected sign of a successful uninstall. The empty
> `/nix` directory will disappear the next time you reboot.
>
> You do not have to reboot to finish uninstalling Nix. The uninstall is
> complete. macOS (Catalina+) directly controls root directories and its
> read-only root will prevent you from manually deleting the empty `/nix`
> mountpoint.

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
