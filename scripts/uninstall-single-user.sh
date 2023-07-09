#!/bin/sh

# Stop the Nix daemon
echo "Stopping Nix daemon..."
if command -v systemctl > /dev/null 2>&1; then
    sudo systemctl stop nix-daemon
elif command -v launchctl > /dev/null 2>&1; then
    sudo launchctl unload /Library/LaunchDaemons/org.nixos.nix-daemon.plist
else
    echo "Couldn't find either systemctl or launchctl, please manually stop the Nix daemon."
fi

# Remove Nix files
echo "Removing Nix files..."
sudo rm -rf /etc/profile.d/nix.sh /etc/nix /var/nix /nix ~root/.nix-profile ~root/.nix-defexpr ~root/.nix-channels ~/.nix-profile ~/.nix-defexpr ~/.nix-channels

# Remove additional Nix files
echo "Removing additional Nix files..."
sudo rm -rf ~/.nix-defexpr ~/.nix-channels ~/.nix-profile

# Restore backup files
echo "Restoring backup files..."
if [ -e "/etc/bash.bashrc.backup-before-nix" ]; then
    sudo cp /etc/bash.bashrc.backup-before-nix /etc/bash.bashrc
fi

if [ -e "/etc/bashrc.backup-before-nix" ]; then
    sudo cp /etc/bashrc.backup-before-nix /etc/bashrc
fi

# Remove nix.sh sourcing from shell profiles
echo "Cleaning up shell profiles..."
for profile in ~/.profile ~/.bash_profile ~/.zshrc
do
    if [ -e "$profile" ]; then
        echo "Cleaning $profile"
        sed -i '/nix.sh/d' $profile
    fi
done

echo "Nix uninstallation complete. Please manually check for remaining Nix-related environment variables in your shell sessions."
