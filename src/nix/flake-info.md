R""(

# Examples

* Show what `nixpkgs` resolves to:

  ```console
  # nix flake info nixpkgs
  Resolved URL:  github:NixOS/nixpkgs
  Locked URL:    github:NixOS/nixpkgs/b67ba0bfcc714453cdeb8d713e35751eb8b4c8f4
  Description:   A collection of packages for the Nix package manager
  Path:          /nix/store/23qapccs6cfmwwrlq8kr41vz5vdmns3r-source
  Revision:      b67ba0bfcc714453cdeb8d713e35751eb8b4c8f4
  Last modified: 2020-12-23 12:36:12
  ```

* Show information about `dwarffs` in JSON format:

  ```console
  # nix flake info dwarffs --json | jq .
  {
    "description": "A filesystem that fetches DWARF debug info from the Internet on demand",
    "lastModified": 1597153508,
    "locked": {
      "lastModified": 1597153508,
      "narHash": "sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0=",
      "owner": "edolstra",
      "repo": "dwarffs",
      "rev": "d181d714fd36eb06f4992a1997cd5601e26db8f5",
      "type": "github"
    },
    "original": {
      "id": "dwarffs",
      "type": "indirect"
    },
    "originalUrl": "flake:dwarffs",
    "path": "/nix/store/hang3792qwdmm2n0d9nsrs5n6bsws6kv-source",
    "resolved": {
      "owner": "edolstra",
      "repo": "dwarffs",
      "type": "github"
    },
    "resolvedUrl": "github:edolstra/dwarffs",
    "revision": "d181d714fd36eb06f4992a1997cd5601e26db8f5",
    "url": "github:edolstra/dwarffs/d181d714fd36eb06f4992a1997cd5601e26db8f5"
  }
  ```

# Description

This command shows information about the flake specified by the flake
reference *flake-url*. It resolves the flake reference using the
[flake registry](./nix3-registry.md), fetches it, and prints some meta
data. This includes:

* `Resolved URL`: If *flake-url* is a flake identifier, then this is
  the flake reference that specifies its actual location, looked up in
  the flake registry.

* `Locked URL`: A flake reference that contains a commit or content
  hash and thus uniquely identifies a specific flake version.

* `Description`: A one-line description of the flake, taken from the
  `description` field in `flake.nix`.

* `Path`: The store path containing the source code of the flake.

* `Revision`: The Git or Mercurial commit hash of the locked flake.

* `Revisions`: The number of ancestors of the Git or Mercurial commit
  of the locked flake. Note that this is not available for `github`
  flakes.

* `Last modified`: For Git or Mercurial flakes, this is the commit
  time of the commit of the locked flake; for tarball flakes, it's the
  most recent timestamp of any file inside the tarball.

With `--json`, the output is a JSON object with the following fields:

* `original` and `originalUrl`: The flake reference specified by the
  user (*flake-url*) in attribute set and URL representation.

* `resolved` and `resolvedUrl`: The resolved flake reference (see
  above) in attribute set and URL representation.

* `locked` and `lockedUrl`: The locked flake reference (see above) in
  attribute set and URL representation.

* `description`: See `Description` above.

* `path`: See `Path` above.

* `revision`: See `Revision` above.

* `revCount`: See `Revisions` above.

* `lastModified`: See `Last modified` above.

)""
