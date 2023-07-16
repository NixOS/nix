# Uninstalling Nix

## Single User

### Manual Method

#### Step 1: Unload the service

1. The first step targets what's called the Nix service "daemon". In computing, a "daemon" is a program that runs behind the scenes instead of directly under a user's control. For Nix, the daemon helps with tasks like updating packages. When uninstalling Nix, it's crucial to stop and delete this daemon first, to prevent it from trying to do tasks during the uninstall process.
   ```bash
   PLIST="/Library/LaunchDaemons/org.nixos.nix-daemon.plist"; if sudo launchctl list | grep -q nix-daemon; then sudo launchctl unload "$PLIST"; fi; if [ -f "$PLIST" ]; then sudo rm /Library/LaunchDaemons/org.nixos.nix-daemon.plist; fi
   ```

      ##### The Command Explained
      1. This sets the path of the Nix daemon's property list file to a variable for easier referencing:

         `PLIST="/Library/LaunchDaemons/org.nixos.nix-daemon.plist"`

      2. This checks if the nix-daemon service is running:

         `sudo launchctl list | grep -q nix-daemon`

      3.  If the Nix service is running, this command unloads it: 
         
            `sudo launchctl unload "$PLIST"`
      
      4.  This checks if the property list file exists:
      
          `[ -f "$PLIST" ]`

      5.  If the property list file exists, this command deletes it:
      
            `sudo rm /Library/LaunchDaemons/org.nixos.nix-daemon.plist`

#### Step 2: Restore Modified Files (if applicable)

1. Nix may have modified certain files on your system. If you have created backup copies of these files before installing Nix, you can restore them. Here are the instructions for bash and zsh files:

   - Bash: Nix may have modified the `/etc/bash.bashrc` or `~/.bash_profile` file. If you have created a backup of this file before installing Nix, you can restore it using the following commands:

     ```bash
     sudo cp /etc/bash.bashrc.backup-before-nix /etc/bash.bashrc
     sudo cp ~/.bash_profile.backup-before-nix ~/.bash_profile
     ```

     Note: After restoring the file, you may need to close and reopen any bash terminal sessions to ensure they are using the restored configurations.

   - Zsh: Nix may have modified the `~/.zshrc` file. If you have created a backup of this file before installing Nix, you can restore it using the following command:

     ```bash
     sudo cp ~/.zshrc.backup-before-nix ~/.zshrc
     ```

     Note: After restoring the file, you may need to close and reopen any zsh terminal sessions to ensure they are using the restored configurations.


#### Step 3: Remove the users and groups

1. This step is used to remove any user and group accounts associated with the Nix service. 
   ```bash
   for i in $(seq 1 $(sysctl -n hw.ncpu)); do sudo /usr/bin/dscl . -delete "/Users/nixbld$i" || true; done; sudo /usr/bin/dscl . -delete "/Groups/nixbld" || true
   ```

   ##### The Command Explained
      1. This generates a sequence of numbers from 1 to the number of logical CPUs on the system. Nix creates a build user for each CPU to parallelize builds, so this ensures that all build users are deleted.

         `seq 1 $(sysctl -n hw.ncpu)`

      2. 
         This deletes each user named "nixbld" followed by a number. If the user doesn't exist (which would cause an error), the || true part causes the command to succeed anyway.

         `sudo /usr/bin/dscl . -delete "/Users/nixbld$i" || true; done`
      
      3.
         This deletes the "nixbld" group. If the group doesn't exist (which would cause an error), the || true part causes the command to succeed anyway: 

         `sudo /usr/bin/dscl . -delete "/Groups/nixbld" || true`


#### Step 4: Delete Nix Files

1. Run the following command in a terminal to delete the files that Nix added to your system:
   ```bash
   sudo rm -rf "/etc/nix" "$HOME/.nix-profile" "$HOME/.nix-defexpr" "$HOME/.nix-channels" "$HOME/.cache/nix" "$NIX_ROOT" "/nix"
   ```

You have successfully uninstalled Nix from your system. Remember to double-check the commands before executing them and ensure that you have the necessary permissions to perform the uninstallation steps.

If you ever need to use Nix again in the future, you can reinstall it following the installation instructions provided by the Nix documentation.


## Multi User

Removing a [multi-user installation](./installing-binary.md#multi-user-installation) of Nix is more involved, and depends on the operating system.

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
sudo rm -rf /etc/nix /etc/profile.d/nix.sh /etc/tmpfiles.d/nix-daemon.conf /nix ~root/.nix-channels ~root/.nix-defexpr ~root/.nix-profile
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

### macOS

1. Edit `/etc/zshrc`, `/etc/bashrc`, and `/etc/bash.bashrc` to remove the lines sourcing `nix-daemon.sh`, which should look like this:

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
   sudo mv /etc/bash.bashrc.backup-before-nix /etc/bash.bashrc
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
   volume on `/nix`, which looks like
   `UUID=<uuid> /nix apfs rw,noauto,nobrowse,suid,owners` or
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

   If the output indicates that the command couldn't remove the volume, you should
   make sure you don't have an _unmounted_ Nix Store volume. Look for a
   "Nix Store" volume in the output of the following command:

   ```console
   diskutil list
   ```

   If you _do_ see a "Nix Store" volume, delete it by re-running the diskutil
   deleteVolume command, but replace `/nix` with the store volume's `diskXsY`
   identifier.

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

