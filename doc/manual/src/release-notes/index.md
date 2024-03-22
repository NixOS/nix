# Nix Release Notes

The Nix release cycle is calendar-based as follows:

- A new minor version (`XX.YY+1.0`) is published every month and supported for two months;
- A new major version (`XX+1.1.0`) is published twice a year, in April and October, and supported for eight months.

The rationale behind that cycle is that
- Minor versions stay close to master and bring early access to new features for the user who need them;
- Major versions are aligned with the NixOS releases (released one month before NixOS and supported for as long at it).

Bugfixes and security issues are backported to every supported version.
Patch releases are published as needed.

Notable changes and additions are announced in the release notes for each version.
