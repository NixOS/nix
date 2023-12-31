# Upgrading Nix

First find the name of the current channel:

```console
$ nix-channel --list
```

Will return:

```console
$ nixpkgs https://nixos.org/channels/nixpkgs-23.05
```
Remove old channel:

```console
$ nix-channel --remove nixpkgs
```

Add new channel:

```console
$ nix-channel --add https://nixos.org/channels/nixpkgs-23.11 nixpkgs
```

Multi-user Nix users on macOS can upgrade Nix by running: `sudo -i sh -c
'nix-channel --update &&
nix-env --install --attr nixpkgs.nix &&
launchctl remove org.nixos.nix-daemon &&
launchctl load /Library/LaunchDaemons/org.nixos.nix-daemon.plist'`

Single-user installations of Nix should run this: `nix-channel --update;
nix-env --install --attr nixpkgs.nix nixpkgs.cacert`

Multi-user Nix users on Linux should run this with sudo: `nix-channel
--update; nix-env --install --attr nixpkgs.nix nixpkgs.cacert; systemctl
daemon-reload; systemctl restart nix-daemon`
