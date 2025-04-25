# Release 3.4.0 (2025-04-25)

* Based on [upstream Nix 2.28.2](../release-notes/rl-2.28.md).

* **Warn users that `nix-channel` is deprecated.**

This is the first change accomplishing our roadmap item of deprecating Nix channels: https://github.com/DeterminateSystems/nix-src/issues/34

This is due to user confusion and surprising behavior of channels, especially in the context of user vs. root channels.

The goal of this change is to make the user experience of Nix more predictable.
In particular, these changes are to support users with lower levels of experience who are following guides that focus on channels as the mechanism of distribution.

Users will now see this message:

> nix-channel is deprecated in favor of flakes in Determinate Nix. For a guide on Nix flakes, see: https://zero-to-nix.com/.  or details and to offer feedback on the deprecation process, see: https://github.com/DeterminateSystems/nix-src/issues/34.


* **Warn users that `channel:` URLs are deprecated.**

This is the second change regarding our deprecation of Nix channels.
Using a `channel:` URL (like `channel:nixos-24.11`) will yield a warning like this:

> Channels are deprecated in favor of flakes in Determinate Nix. Instead of 'channel:nixos-24.11', use 'https://nixos.org/channels/nixos-24.11/nixexprs.tar.xz'. For a guide on Nix flakes, see: https://zero-to-nix.com/. For details and to offer feedback on the deprecation process, see: https://github.com/DeterminateSystems/nix-src/issues/34.

* **Warn users against indirect flake references in `flake.nix` inputs**

This is the first change accomplishing our roadmap item of deprecating implicit and indirect flake inputs: https://github.com/DeterminateSystems/nix-src/issues/37

The flake registry provides an important UX affordance for using Nix flakes and remote sources in command line uses.
For that reason, the registry is not being deprecated entirely and will still be used for command-line incantations, like nix run.

This move will eliminate user confusion and surprising behavior around global and local registries during flake input resolution.

The goal of this change is to make the user experience of Nix more predictable.
We have seen a pattern of confusion when using automatic flake inputs and local registries.
Specifically, users' flake inputs resolving and locking inconsistently depending on the configuration of the host system.

Users will now see the following warning if their flake.nix uses an implicit or indirect Flake reference input:

> Flake input 'nixpkgs' uses the flake registry. Using the registry in flake inputs is deprecated in Determinate Nix. To make your flake future-proof, add the following to 'xxx/flake.nix':
>
>  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
>
> For more information, see: https://github.com/DeterminateSystems/nix-src/issues/37


### Other updates:
* Improve the "dirty tree" message. Determinate Nix will now say `Git tree '...' has uncommitted changes` instead of `Git tree '...' is dirty`
* Stop warning about uncommitted changes in a Git repository when using `nix develop`
