R""(

# Examples

* Show what got added and removed between two versions of the NixOS
  system profile:

  ```console
  # nix store diff-closures /nix/var/nix/profiles/system-655-link /nix/var/nix/profiles/system-658-link
  acpi-call: 2020-04-07-5.8.16 → 2020-04-07-5.8.18
  baloo-widgets: 20.08.1 → 20.08.2
  bluez-qt: +12.6 KiB
  dolphin: 20.08.1 → 20.08.2, +13.9 KiB
  kdeconnect: 20.08.2 → ∅, -6597.8 KiB
  kdeconnect-kde: ∅ → 20.08.2, +6599.7 KiB
  …
  ```

# Description

This command shows the differences between the two closures *before*
and *after* with respect to the addition, removal, or version change
of packages, as well as changes in store path sizes.

For each package name in the two closures (where a package name is
defined as the name component of a store path excluding the version),
if there is a change in the set of versions of the package, or a
change in the size of the store paths of more than 8 KiB, it prints a
line like this:

```console
dolphin: 20.08.1 → 20.08.2, +13.9 KiB
```

No size change is shown if it's below the threshold. If the package
does not exist in either the *before* or *after* closures, it is
represented using `∅` (empty set) on the appropriate side of the
arrow. If a package has an empty version string, the version is
rendered as `ε` (epsilon).

There may be multiple versions of a package in each closure. In that
case, only the changed versions are shown. Thus,

```console
libfoo: 1.2, 1.3 → 1.4
```

leaves open the possibility that there are other versions (e.g. `1.1`)
that exist in both closures.

)""
