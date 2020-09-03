# Installing a Binary Distribution

If you are using Linux or macOS versions up to 10.14 (Mojave), the
easiest way to install Nix is to run the following command:

```console
$ sh <(curl -L https://nixos.org/nix/install)
```

If you're using macOS 10.15 (Catalina) or newer, consult [the macOS
installation instructions](#macos-installation) before installing.

As of Nix 2.1.0, the Nix installer will always default to creating a
single-user installation, however opting in to the multi-user
installation is highly recommended.

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

# macOS Installation

Starting with macOS 10.15 (Catalina), the root filesystem is read-only.
This means `/nix` can no longer live on your system volume, and that
you'll need a workaround to install Nix.

The recommended approach, which creates an unencrypted APFS volume for
your Nix store and a "synthetic" empty directory to mount it over at
`/nix`, is least likely to impair Nix or your system.

> **Note**
> 
> With all separate-volume approaches, it's possible something on your
> system (particularly daemons/services and restored apps) may need
> access to your Nix store before the volume is mounted. Adding
> additional encryption makes this more likely.

If you're using a recent Mac with a [T2
chip](https://www.apple.com/euro/mac/shared/docs/Apple_T2_Security_Chip_Overview.pdf),
your drive will still be encrypted at rest (in which case "unencrypted"
is a bit of a misnomer). To use this approach, just install Nix with:

```console
$ sh <(curl -L https://nixos.org/nix/install) --darwin-use-unencrypted-nix-store-volume
```

If you don't like the sound of this, you'll want to weigh the other
approaches and tradeoffs detailed in this section.

> **Note**
> 
> All of the known workarounds have drawbacks, but we hope better
> solutions will be available in the future. Some that we have our eye
> on are:
> 
> 1.  A true firmlink would enable the Nix store to live on the primary
>     data volume without the build problems caused by the symlink
>     approach. End users cannot currently create true firmlinks.
> 
> 2.  If the Nix store volume shared FileVault encryption with the
>     primary data volume (probably by using the same volume group and
>     role), FileVault encryption could be easily supported by the
>     installer without requiring manual setup by each user.

## Change the Nix store path prefix

Changing the default prefix for the Nix store is a simple approach which
enables you to leave it on your root volume, where it can take full
advantage of FileVault encryption if enabled. Unfortunately, this
approach also opts your device out of some benefits that are enabled by
using the same prefix across systems:

  - Your system won't be able to take advantage of the binary cache
    (unless someone is able to stand up and support duplicate caching
    infrastructure), which means you'll spend more time waiting for
    builds.

  - It's harder to build and deploy packages to Linux systems.

It would also possible (and often requested) to just apply this change
ecosystem-wide, but it's an intrusive process that has side effects we
want to avoid for now.

## Use a separate encrypted volume

If you like, you can also add encryption to the recommended approach
taken by the installer. You can do this by pre-creating an encrypted
volume before you run the installer--or you can run the installer and
encrypt the volume it creates later.

In either case, adding encryption to a second volume isn't quite as
simple as enabling FileVault for your boot volume. Before you dive in,
there are a few things to weigh:

1.  The additional volume won't be encrypted with your existing
    FileVault key, so you'll need another mechanism to decrypt the
    volume.

2.  You can store the password in Keychain to automatically decrypt the
    volume on boot--but it'll have to wait on Keychain and may not mount
    before your GUI apps restore. If any of your launchd agents or apps
    depend on Nix-installed software (for example, if you use a
    Nix-installed login shell), the restore may fail or break.
    
    On a case-by-case basis, you may be able to work around this problem
    by using `wait4path` to block execution until your executable is
    available.
    
    It's also possible to decrypt and mount the volume earlier with a
    login hook--but this mechanism appears to be deprecated and its
    future is unclear.

3.  You can hard-code the password in the clear, so that your store
    volume can be decrypted before Keychain is available.

If you are comfortable navigating these tradeoffs, you can encrypt the
volume with something along the lines of:

```console
alice$ diskutil apfs enableFileVault /nix -user disk
```

## Symlink the Nix store to a custom location

Another simple approach is using `/etc/synthetic.conf` to symlink the
Nix store to the data volume. This option also enables your store to
share any configured FileVault encryption. Unfortunately, builds that
resolve the symlink may leak the canonical path or even fail.

Because of these downsides, we can't recommend this approach.

## Notes on the recommended approach

This section goes into a little more detail on the recommended approach.
You don't need to understand it to run the installer, but it can serve
as a helpful reference if you run into trouble.

1.  In order to compose user-writable locations into the new read-only
    system root, Apple introduced a new concept called `firmlinks`,
    which it describes as a "bi-directional wormhole" between two
    filesystems. You can see the current firmlinks in
    `/usr/share/firmlinks`. Unfortunately, firmlinks aren't (currently?)
    user-configurable.
    
    For special cases like NFS mount points or package manager roots,
    [synthetic.conf(5)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man5/synthetic.conf.5.html)
    supports limited user-controlled file-creation (of symlinks, and
    synthetic empty directories) at `/`. To create a synthetic empty
    directory for mounting at `/nix`, add the following line to
    `/etc/synthetic.conf` (create it if necessary):
    
        nix

2.  This configuration is applied at boot time, but you can use
    `apfs.util` to trigger creation (not deletion) of new entries
    without a reboot:
    
    ```console
    alice$ /System/Library/Filesystems/apfs.fs/Contents/Resources/apfs.util -B
    ```

3.  Create the new APFS volume with diskutil:
    
    ```console
    alice$ sudo diskutil apfs addVolume diskX APFS 'Nix Store' -mountpoint /nix
    ```

4.  Using `vifs`, add the new mount to `/etc/fstab`. If it doesn't
    already have other entries, it should look something like:
    
        #
        # Warning - this file should only be modified with vifs(8)
        #
        # Failure to do so is unsupported and may be destructive.
        #
        LABEL=Nix\040Store /nix apfs rw,nobrowse
    
    The nobrowse setting will keep Spotlight from indexing this volume,
    and keep it from showing up on your desktop.

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
alice$ cd /tmp
alice$ tar xfj nix-1.8-x86_64-darwin.tar.bz2
alice$ cd nix-1.8-x86_64-darwin
alice$ ./install
```

If you need to edit the multi-user installation script to use different
group ID or a different user ID range, modify the variables set in the
file named `install-multi-user`.
