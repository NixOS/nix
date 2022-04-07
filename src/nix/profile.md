R""(

# Description

`nix profile` allows you to create and manage *Nix profiles*. A Nix
profile is a set of packages that can be installed and upgraded
independently from each other. Nix profiles are versioned, allowing
them to be rolled back easily.

# Default profile

The default profile used by `nix profile` is `$HOME/.nix-profile`,
which, if it does not exist, is created as a symlink to
`/nix/var/nix/profiles/per-user/default` if Nix is invoked by the
`root` user, or `/nix/var/nix/profiles/per-user/`*username* otherwise.

You can specify another profile location using `--profile` *path*.

# Filesystem layout

Profiles are versioned as follows. When using profile *path*, *path*
is a symlink to *path*`-`*N*, where *N* is the current *version* of
the profile. In turn, *path*`-`*N* is a symlink to a path in the Nix
store. For example:

```console
$ ls -l /nix/var/nix/profiles/per-user/alice/profile*
lrwxrwxrwx 1 alice users 14 Nov 25 14:35 /nix/var/nix/profiles/per-user/alice/profile -> profile-7-link
lrwxrwxrwx 1 alice users 51 Oct 28 16:18 /nix/var/nix/profiles/per-user/alice/profile-5-link -> /nix/store/q69xad13ghpf7ir87h0b2gd28lafjj1j-profile
lrwxrwxrwx 1 alice users 51 Oct 29 13:20 /nix/var/nix/profiles/per-user/alice/profile-6-link -> /nix/store/6bvhpysd7vwz7k3b0pndn7ifi5xr32dg-profile
lrwxrwxrwx 1 alice users 51 Nov 25 14:35 /nix/var/nix/profiles/per-user/alice/profile-7-link -> /nix/store/mp0x6xnsg0b8qhswy6riqvimai4gm677-profile
```

Each of these symlinks is a root for the Nix garbage collector.

The contents of the store path corresponding to each version of the
profile is a tree of symlinks to the files of the installed packages,
e.g.

```console
$ ll -R /nix/var/nix/profiles/per-user/eelco/profile-7-link/
/nix/var/nix/profiles/per-user/eelco/profile-7-link/:
total 20
dr-xr-xr-x 2 root root 4096 Jan  1  1970 bin
-r--r--r-- 2 root root 1402 Jan  1  1970 manifest.json
dr-xr-xr-x 4 root root 4096 Jan  1  1970 share

/nix/var/nix/profiles/per-user/eelco/profile-7-link/bin:
total 20
lrwxrwxrwx 5 root root 79 Jan  1  1970 chromium -> /nix/store/ijm5k0zqisvkdwjkc77mb9qzb35xfi4m-chromium-86.0.4240.111/bin/chromium
lrwxrwxrwx 7 root root 87 Jan  1  1970 spotify -> /nix/store/w9182874m1bl56smps3m5zjj36jhp3rn-spotify-1.1.26.501.gbe11e53b-15/bin/spotify
lrwxrwxrwx 3 root root 79 Jan  1  1970 zoom-us -> /nix/store/wbhg2ga8f3h87s9h5k0slxk0m81m4cxl-zoom-us-5.3.469451.0927/bin/zoom-us

/nix/var/nix/profiles/per-user/eelco/profile-7-link/share/applications:
total 12
lrwxrwxrwx 4 root root 120 Jan  1  1970 chromium-browser.desktop -> /nix/store/4cf803y4vzfm3gyk3vzhzb2327v0kl8a-chromium-unwrapped-86.0.4240.111/share/applications/chromium-browser.desktop
lrwxrwxrwx 7 root root 110 Jan  1  1970 spotify.desktop -> /nix/store/w9182874m1bl56smps3m5zjj36jhp3rn-spotify-1.1.26.501.gbe11e53b-15/share/applications/spotify.desktop
lrwxrwxrwx 3 root root 107 Jan  1  1970 us.zoom.Zoom.desktop -> /nix/store/wbhg2ga8f3h87s9h5k0slxk0m81m4cxl-zoom-us-5.3.469451.0927/share/applications/us.zoom.Zoom.desktop

…
```

The file `manifest.json` records the provenance of the packages that
are installed in this version of the profile. It looks like this:

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

* `originalUrl`: The [flake reference](./nix3-flake.md) specified by
  the user at the time of installation (e.g. `nixpkgs`). This is also
  the flake reference that will be used by `nix profile upgrade`.

* `uri`: The immutable flake reference to which `originalUrl`
  resolved.

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

)""
