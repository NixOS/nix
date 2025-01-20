## `manifest.json`

The manifest file records the provenance of the packages that are installed in a [profile](./profiles.md) managed by [`nix profile`](@docroot@/command-ref/new-cli/nix3-profile.md) (experimental).

Here is an example of what the file might look like after installing `zoom-us` from Nixpkgs:

```json
{
  "version": 1,
  "elements": [
    {
      "active": true,
      "attrPath": "legacyPackages.x86_64-linux.zoom-us",
      "originalUrl": "flake:nixpkgs",
      "storePaths": [
        "/nix/store/wbhg2ga8f3h87s9h5k0slxk0m81m4cxl-zoom-us-5.3.469451.0927"
      ],
      "uri": "github:NixOS/nixpkgs/13d0c311e3ae923a00f734b43fd1d35b47d8943a"
    },
    …
  ]
}
```

Each object in the array `elements` denotes an installed package and
has the following fields:

* `originalUrl`: The [flake reference](@docroot@/command-ref/new-cli/nix3-flake.md) specified by
  the user at the time of installation (e.g. `nixpkgs`). This is also
  the flake reference that will be used by `nix profile upgrade`.

* `uri`: The locked flake reference to which `originalUrl` resolved.

* `attrPath`: The flake output attribute that provided this
  package. Note that this is not necessarily the attribute that the
  user specified, but the one resulting from applying the default
  attribute paths and prefixes; for instance, `hello` might resolve to
  `packages.x86_64-linux.hello` and the empty string to
  `packages.x86_64-linux.default`.

* `storePath`: The paths in the Nix store containing the package.

* `active`: Whether the profile contains symlinks to the files of this
  package. If set to false, the package is kept in the Nix store, but
  is not "visible" in the profile's symlink tree.
