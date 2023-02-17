# User profiles

A directory that contains links to user profiles managed by [`nix-env`] and [`nix profile`].

- `$NIX_STATE_DIR/profiles/per-user/root` if the user is `root`
- `$XDG_STATE_HOME/nix/profiles` otherwise

A profile is a directory of symlinks to files in the Nix store.

## Profile compatibility

> **Warning**
>
> `nix profile` is experimental.
> Once you have used `nix profile` you can no longer use `nix-env` without first deleting `$XDG_STATE_HOME/nix/profiles/profile`

Once you installed a package with `nix profile`, you get the following
error message when using `nix-env`:

```console
$ nix-env -f '<nixpkgs>' -iA 'hello'
error: --- Error ----------------------------------------------------------------------------------------------------------------- nix-env
profile '/home/alice/.local/state/nix/profiles/profile' is incompatible with 'nix-env'; please use 'nix profile' instead
```

To migrate back to `nix-env` you can delete your current profile:

> **Warning**
>
> This will delete packages that have been installed before, so you may want to back this information before running the command.

```console
 $ rm -rf "${XDG_STATE_HOME-$HOME/.local/state}/nix/profiles/profile"
```

## Filesystem layout

Profiles are versioned as follows. When using profile *path*, *path*
is a symlink to *path*`-`*N*, where *N* is the current *version* of
the profile. In turn, *path*`-`*N* is a symlink to a path in the Nix
store. For example:

```console
$ ls -l ~alice/.local/state/nix/profiles/profile*
lrwxrwxrwx 1 alice users 14 Nov 25 14:35 /home/alice/.local/state/nix/profiles/profile -> profile-7-link
lrwxrwxrwx 1 alice users 51 Oct 28 16:18 /home/alice/.local/state/nix/profiles/profile-5-link -> /nix/store/q69xad13ghpf7ir87h0b2gd28lafjj1j-profile
lrwxrwxrwx 1 alice users 51 Oct 29 13:20 /home/alice/.local/state/nix/profiles/profile-6-link -> /nix/store/6bvhpysd7vwz7k3b0pndn7ifi5xr32dg-profile
lrwxrwxrwx 1 alice users 51 Nov 25 14:35 /home/alice/.local/state/nix/profiles/profile-7-link -> /nix/store/mp0x6xnsg0b8qhswy6riqvimai4gm677-profile
```

Each of these symlinks is a root for the Nix garbage collector.

The contents of the store path corresponding to each version of the
profile is a tree of symlinks to the files of the installed packages,
e.g.

```console
$ ll -R ~eelco/.local/state/nix/profiles/profile-7-link/
/home/eelco/.local/state/nix/profiles/profile-7-link/:
total 20
dr-xr-xr-x 2 root root 4096 Jan  1  1970 bin
-r--r--r-- 2 root root 1402 Jan  1  1970 manifest.json
dr-xr-xr-x 4 root root 4096 Jan  1  1970 share

/home/eelco/.local/state/nix/profiles/profile-7-link/bin:
total 20
lrwxrwxrwx 5 root root 79 Jan  1  1970 chromium -> /nix/store/ijm5k0zqisvkdwjkc77mb9qzb35xfi4m-chromium-86.0.4240.111/bin/chromium
lrwxrwxrwx 7 root root 87 Jan  1  1970 spotify -> /nix/store/w9182874m1bl56smps3m5zjj36jhp3rn-spotify-1.1.26.501.gbe11e53b-15/bin/spotify
lrwxrwxrwx 3 root root 79 Jan  1  1970 zoom-us -> /nix/store/wbhg2ga8f3h87s9h5k0slxk0m81m4cxl-zoom-us-5.3.469451.0927/bin/zoom-us

/home/eelco/.local/state/nix/profiles/profile-7-link/share/applications:
total 12
lrwxrwxrwx 4 root root 120 Jan  1  1970 chromium-browser.desktop -> /nix/store/4cf803y4vzfm3gyk3vzhzb2327v0kl8a-chromium-unwrapped-86.0.4240.111/share/applications/chromium-browser.desktop
lrwxrwxrwx 7 root root 110 Jan  1  1970 spotify.desktop -> /nix/store/w9182874m1bl56smps3m5zjj36jhp3rn-spotify-1.1.26.501.gbe11e53b-15/share/applications/spotify.desktop
lrwxrwxrwx 3 root root 107 Jan  1  1970 us.zoom.Zoom.desktop -> /nix/store/wbhg2ga8f3h87s9h5k0slxk0m81m4cxl-zoom-us-5.3.469451.0927/share/applications/us.zoom.Zoom.desktop

…
```

## Manifest

The manifest file records the provenance of the packages that are installed in this version of the profile.
It is different between [`nix-env`] and [`nix profile`].

### `manifest.nix` 

Used by [`nix-env`]. This is an example of how this file might look like after installing `hello` from nixpkgs.

```nix
[{
  meta = {
    available = true;
    broken = false;
    changelog =
      "https://git.savannah.gnu.org/cgit/hello.git/plain/NEWS?h=v2.12.1";
    description = "A program that produces a familiar, friendly greeting";
    homepage = "https://www.gnu.org/software/hello/manual/";
    insecure = false;
    license = {
      deprecated = false;
      free = true;
      fullName = "GNU General Public License v3.0 or later";
      redistributable = true;
      shortName = "gpl3Plus";
      spdxId = "GPL-3.0-or-later";
      url = "https://spdx.org/licenses/GPL-3.0-or-later.html";
    };
    longDescription = ''
      GNU Hello is a program that prints "Hello, world!" when you run it.
      It is fully customizable.
    '';
    maintainers = [{
      email = "edolstra+nixpkgs@gmail.com";
      github = "edolstra";
      githubId = 1148549;
      name = "Eelco Dolstra";
    }];
    name = "hello-2.12.1";
    outputsToInstall = [ "out" ];
    platforms = [
      "i686-cygwin"
      "x86_64-cygwin"
      "x86_64-darwin"
      "i686-darwin"
      "aarch64-darwin"
      "armv7a-darwin"
      "i686-freebsd13"
      "x86_64-freebsd13"
      "aarch64-genode"
      "i686-genode"
      "x86_64-genode"
      "x86_64-solaris"
      "js-ghcjs"
      "aarch64-linux"
      "armv5tel-linux"
      "armv6l-linux"
      "armv7a-linux"
      "armv7l-linux"
      "i686-linux"
      "m68k-linux"
      "microblaze-linux"
      "microblazeel-linux"
      "mipsel-linux"
      "mips64el-linux"
      "powerpc64-linux"
      "powerpc64le-linux"
      "riscv32-linux"
      "riscv64-linux"
      "s390-linux"
      "s390x-linux"
      "x86_64-linux"
      "mmix-mmixware"
      "aarch64-netbsd"
      "armv6l-netbsd"
      "armv7a-netbsd"
      "armv7l-netbsd"
      "i686-netbsd"
      "m68k-netbsd"
      "mipsel-netbsd"
      "powerpc-netbsd"
      "riscv32-netbsd"
      "riscv64-netbsd"
      "x86_64-netbsd"
      "aarch64_be-none"
      "aarch64-none"
      "arm-none"
      "armv6l-none"
      "avr-none"
      "i686-none"
      "microblaze-none"
      "microblazeel-none"
      "msp430-none"
      "or1k-none"
      "m68k-none"
      "powerpc-none"
      "powerpcle-none"
      "riscv32-none"
      "riscv64-none"
      "rx-none"
      "s390-none"
      "s390x-none"
      "vc4-none"
      "x86_64-none"
      "i686-openbsd"
      "x86_64-openbsd"
      "x86_64-redox"
      "wasm64-wasi"
      "wasm32-wasi"
      "x86_64-windows"
      "i686-windows"
    ];
    position =
      "/nix/store/7niq32w715567hbph0q13m5lqna64c1s-nixos-unstable.tar.gz/nixos-unstable.tar.gz/pkgs/applications/misc/hello/default.nix:34";
    unfree = false;
    unsupported = false;
  };
  name = "hello-2.12.1";
  out = {
    outPath = "/nix/store/260q5867crm1xjs4khgqpl6vr9kywql1-hello-2.12.1";
  };
  outPath = "/nix/store/260q5867crm1xjs4khgqpl6vr9kywql1-hello-2.12.1";
  outputs = [ "out" ];
  system = "x86_64-linux";
  type = "derivation";
}]
```

Each element in this list corresponds to an installed package.
It incorporates some attributes of the original package derivation, including `meta`, `name`, `out`, `outPath`, `outputs`, `system`.
This is used by Nix for querying and updating the package.

### `manifest.json`

Used by [`nix profile`]. This is an example of what the file might look like after installing `zoom-us` from nixpkgs:

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

## User profile link

A symbolic link to the user's current profile. 

- `~/.nix-profile`
- `$XDG_STATE_HOME/nix/profile` if [`use-xdg-base-directories`] is set to `true`.

By default, this symlink points to:

- `$XDG_STATE_HOME/nix/profiles/profile` for regular users
- `$NIX_STATE_DIR/profiles/per-user/root/profile` for `root`

The `PATH` environment variable should include `/bin` subdirectory of the profile link (e.g. `~/.nix-profile/bin`) for the user environment to be visible to the user.
The [installer](@docroot@/installation/installing-binary.md) sets this up by default, unless you enable [`use-xdg-base-directories`].

[`nix-env`]: @docroot@/command-ref/nix-env.md
[`nix profile`]: @docroot@/command-ref/new-cli/nix3-profile.md
[`use-xdg-base-directories`]: @docroot@/command-ref/conf-file.md#conf-use-xdg-base-directories
