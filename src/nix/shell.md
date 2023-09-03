R""(

# Examples

* Start a shell providing `youtube-dl` from the `nixpkgs` flake:

  ```console
  # nix shell nixpkgs#youtube-dl
  # youtube-dl --version
  2020.11.01.1
  ```

* Start a shell providing GNU Hello from NixOS 20.03:

  ```console
  # nix shell nixpkgs/nixos-20.03#hello
  ```

* Run GNU Hello:

  ```console
  # nix shell nixpkgs#hello --command hello --greeting 'Hi everybody!'
  Hi everybody!
  ```

* Run multiple commands in a shell environment:

  ```console
  # nix shell nixpkgs#gnumake --command sh --command "cd src && make"
  ```

* Run GNU Hello in a chroot store:

  ```console
  # nix shell --store ~/my-nix nixpkgs#hello --command hello
  ```

* Start a shell providing GNU Hello in a chroot store:

  ```console
  # nix shell --store ~/my-nix nixpkgs#hello nixpkgs#bashInteractive --command bash
  ```

  Note that it's necessary to specify `bash` explicitly because your
  default shell (e.g. `/bin/bash`) generally will not exist in the
  chroot.

# Description

`nix shell` runs a command in an environment in which the `$PATH` variable
provides the specified [*installables*](./nix.md#installable). If no command is specified, it starts the
default shell of your user account specified by `$SHELL`.

)""
