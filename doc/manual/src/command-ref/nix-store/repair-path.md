# Name

`nix --repair-path` - re-download path from substituter

# Synopsis

`nix-store` `--repair-path` *paths…*

# Description

The operation `--repair-path` attempts to “repair” the specified paths
by redownloading them using the available substituters. If no
substitutes are available, then repair is not possible.

> **Warning**
>
> During repair, there is a very small time window during which the old
> path (if it exists) is moved out of the way and replaced with the new
> path. If repair is interrupted in between, then the system may be left
> in a broken state (e.g., if the path contains a critical system
> component like the GNU C Library).

# Example

```console
$ nix-store --verify-path /nix/store/dj7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13
path `/nix/store/dj7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13' was modified!
  expected hash `2db57715ae90b7e31ff1f2ecb8c12ec1cc43da920efcbe3b22763f36a1861588',
  got `481c5aa5483ebc97c20457bb8bca24deea56550d3985cda0027f67fe54b808e4'

$ nix-store --repair-path /nix/store/dj7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13
fetching path `/nix/store/d7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13'...
…
```

