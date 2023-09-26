# Nix Release Notes

Nix has a release cycle of roughly 6 weeks.
Notable changes and additions are announced in the release notes for each version.

Bugfixes can be backported on request to previous Nix releases.
We typically backport only as far back as the Nix version used in the latest NixOS release, which is announced in the [NixOS release notes](https://nixos.org/manual/nixos/stable/release-notes.html#ch-release-notes).

Backports never skip releases.
If a feature is backported to version `x.y`, it must also be available in version `x.(y+1)`.
This ensures that upgrading from an older version with backports is still safe and no backported functionality will go missing.

