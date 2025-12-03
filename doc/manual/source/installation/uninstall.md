# Uninstalling Nix

## Multi User

Removing a [multi-user installation](./installing-binary.md#multi-user-installation) depends on the operating system.

### Linux

If you are on Linux with systemd:

1. Remove the Nix daemon service:

   ```console
   sudo systemctl stop nix-daemon.service
   sudo systemctl disable nix-daemon.socket nix-daemon.service
   sudo systemctl daemon-reload
   ```

Remove files created by Nix:

```console
sudo rm -rf /etc/nix /etc/profile.d/nix.sh /etc/tmpfiles.d/nix-daemon.conf /nix ~root/.nix-channels ~root/.nix-defexpr ~root/.nix-profile ~root/.cache/nix
```

Remove build users and their group:

```console
for i in $(seq 1 32); do
  sudo userdel nixbld$i
done
sudo groupdel nixbld
```

There may also be references to Nix in

- `/etc/bash.bashrc`
- `/etc/bashrc`
- `/etc/profile`
- `/etc/zsh/zshrc`
- `/etc/zshrc`

which you may remove.

### FreeBSD

1. Stop and remove the Nix daemon service:

   ```console
   sudo service nix-daemon stop
   sudo rm -f /usr/local/etc/rc.d/nix-daemon
   sudo sysrc -x nix_daemon_enable
   ```

2. Remove files created by Nix:

   ```console
   sudo rm -rf /etc/nix /usr/local/etc/profile.d/nix.sh /nix ~root/.nix-channels ~root/.nix-defexpr ~root/.nix-profile ~root/.cache/nix
   ```

3. Remove build users and their group:

   ```console
   for i in $(seq 1 32); do
     sudo pw userdel nixbld$i
   done
   sudo pw groupdel nixbld
   ```

4. There may also be references to Nix in:
   - `/usr/local/etc/bashrc`
   - `/usr/local/etc/zshrc`
   - Shell configuration files in users' home directories

   which you may remove.

### macOS

> **Updating to macOS 15 Sequoia**
>
> If you recently updated to macOS 15 Sequoia and are getting
> ```console
> error: the user '_nixbld1' in the group 'nixbld' does not exist
> ```
> when running Nix commands, refer to GitHub issue [NixOS/nix#10892](https://github.com/NixOS/nix/issues/10892) for instructions to fix your installation without reinstalling.

1. If system-wide shell initialisation files haven't been altered since installing Nix, use the backups made by the installer:

   ```console
   sudo mv /etc/zshrc.backup-before-nix /etc/zshrc
   sudo mv /etc/bashrc.backup-before-nix /etc/bashrc
   sudo mv /etc/bash.bashrc.backup-before-nix /etc/bash.bashrc
   ```

   Otherwise, edit `/etc/zshrc`, `/etc/bashrc`, and `/etc/bash.bashrc` to remove the lines sourcing `nix-daemon.sh`, which should look like this:

   ```bash
   # Nix
   if [ -e '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh' ]; then
     . '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh'
   fi
   # End Nix
   ```

2. Stop and remove the Nix daemon services:

   ```console
   sudo launchctl unload /Library/LaunchDaemons/org.nixos.nix-daemon.plist
   sudo rm /Library/LaunchDaemons/org.nixos.nix-daemon.plist
   sudo launchctl unload /Library/LaunchDaemons/org.nixos.darwin-store.plist
   sudo rm /Library/LaunchDaemons/org.nixos.darwin-store.plist
   ```

   This stops the Nix daemon and prevents it from being started next time you boot the system.

3. Remove the `nixbld` group and the `_nixbuildN` users:

   ```console
   sudo dscl . -delete /Groups/nixbld
   for u in $(sudo dscl . -list /Users | grep _nixbld); do sudo dscl . -delete /Users/$u; done
   ```

   This will remove all the build users that no longer serve a purpose.

4. Edit fstab using `sudo vifs` to remove the line mounting the Nix Store volume on `/nix`, which looks like

   ```
   UUID=<uuid> /nix apfs rw,noauto,nobrowse,suid,owners
   ```
   or

   ```
   LABEL=Nix\040Store /nix apfs rw,nobrowse
   ```

   by setting the cursor on the respective line using the arrow keys, and pressing `dd`, and then `:wq` to save the file.

   This will prevent automatic mounting of the Nix Store volume.

5. Edit `/etc/synthetic.conf` to remove the `nix` line.
   If this is the only line in the file you can remove it entirely:

   ```bash
   if [ -f /etc/synthetic.conf ]; then
     if [ "$(cat /etc/synthetic.conf)" = "nix" ]; then
       sudo rm /etc/synthetic.conf
     else
       sudo vi /etc/synthetic.conf
     fi
   fi
   ```

   This will prevent the creation of the empty `/nix` directory.

6. Remove the files Nix added to your system, except for the store:

   ```console
   sudo rm -rf /etc/nix /var/root/.nix-profile /var/root/.nix-defexpr /var/root/.nix-channels ~/.nix-profile ~/.nix-defexpr ~/.nix-channels
   ```


7. Remove the Nix Store volume:

   ```console
   sudo diskutil apfs deleteVolume /nix
   ```

   This will remove the Nix Store volume and everything that was added to the store.

   If the output indicates that the command couldn't remove the volume, you should make sure you don't have an _unmounted_ Nix Store volume.
   Look for a "Nix Store" volume in the output of the following command:

   ```console
   diskutil list
   ```

   If you _do_ find a "Nix Store" volume, delete it by running `diskutil apfs deleteVolume` with the store volume's `diskXsY` identifier.

   If you get an error that the volume is in use by the kernel, reboot and immediately delete the volume before starting any other process.

> **Note**
>
> After you complete the steps here, you will still have an empty `/nix` directory.
> This is an expected sign of a successful uninstall.
> The empty `/nix` directory will disappear the next time you reboot.
>
> You do not have to reboot to finish uninstalling Nix.
> The uninstall is complete.
> macOS (Catalina+) directly controls root directories, and its read-only root will prevent you from manually deleting the empty `/nix` mountpoint.

## Single User

To remove a [single-user installation](./installing-binary.md#single-user-installation) of Nix, run:

```console
rm -rf /nix ~/.nix-channels ~/.nix-defexpr ~/.nix-profile
```
You might also want to manually remove references to Nix from your `~/.profile`.
