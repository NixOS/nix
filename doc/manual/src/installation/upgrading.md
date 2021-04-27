# Upgrading Nix

Multi-user Nix users on macOS can upgrade Nix by running: `sudo -i sh -c
'nix-channel --update &&
nix-env -iA nixpkgs.nix &&
launchctl remove org.nixos.nix-daemon &&
launchctl load /Library/LaunchDaemons/org.nixos.nix-daemon.plist'`

Single-user installations of Nix should run this: `nix-channel --update;
nix-env -iA nixpkgs.nix nixpkgs.cacert`

Multi-user Nix users on Linux should run this with sudo: `nix-channel
--update; nix-env -iA nixpkgs.nix nixpkgs.cacert; systemctl
daemon-reload; systemctl restart nix-daemon`
