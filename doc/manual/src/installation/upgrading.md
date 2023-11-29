# Upgrading Nix

## MacOS

### Multi-user

Run: `sudo -i sh -c
'nix-channel --update &&
nix-env --install --attr nixpkgs.nix &&
launchctl remove org.nixos.nix-daemon &&
launchctl load /Library/LaunchDaemons/org.nixos.nix-daemon.plist'`

### Single-user<a id='macos-single-user'></a>

Run: `nix-channel --update;
nix-env --install --attr nixpkgs.nix nixpkgs.cacert`

## Linux

### Multi-user

Run: `nix-channel
--update; nix-env --install --attr nixpkgs.nix nixpkgs.cacert; systemctl
daemon-reload; systemctl restart nix-daemon`

### Single-user

Same as [MacOS](#macos-single-user)

## NixOS

### Without flake
Run this with sudo: `nix-channel --update; nixos-rebuild switch`

If that doesn't update the version you will have to add a more recent channel (with sudo): `nix-channel --add <channel-url> nixos; nix-channel --update; nixos-rebuild switch`

### With flake
Update the nixpkgs commit with: `nix flake update` or update `inputs.nixpkgs.url` to be more specific about the source, check out some examples [here](https://nixos.org/manual/nix/stable/command-ref/new-cli/nix3-flake.html#examples).

After updating the source of nixpkgs, run this with sudo: `nixos-rebuild switch --flake .#<configuration-name>`
