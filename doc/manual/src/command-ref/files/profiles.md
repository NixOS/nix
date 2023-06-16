## Profile Directory

A directory that contains profiles.
Profiles are typically managed by [`nix-env`] and [`nix profile`].
These are the default profile directories:

- `$XDG_STATE_HOME/nix/profiles` for regular users
- `$NIX_STATE_DIR/profiles/per-user/root` if the user is `root`

## Profile

Profiles are a basic version control mechanism built atop more core Nix constructs and symlinks.
Profiles consist of

- A map of zero or more generations, each mapping a *generation number* to a [store object](@docroot@/glossary.md#gloss-store-object).

- An optional *current* or *active* generation, which is one of those in the map.

Profiles are represented on disk by symlinks within a directory that obey a certain structure.
Profiles have names, which allows storing more than one profile in the same directory.
The symlinks of a single profile all begin with the name of profile, which identifies which symlinks belong to which profile within a directory.
A combined `<profile-directory>/<profile-name>` path uniquely identifies a profile on the local file system, by specifying both its location and name.

Each generation is represented by a symlink with a name with 3 dash-separated components: `<profile-name>-<generation-number>-link`.
The leading `profile-` identifies which profile the generation belongs to.
The trailing `-link` identifies this symlink as a generation symlink.
The middle generation number is the "key" part of the generation key-value pair.
The symlink target is a store paths, which in turn point to the store object that is the "value" part of the generation key-value pair.
The creation time of the symlink is used for time-based garbage collection operations of generations.
Other metadata is not used.

The current/active generation is specified by a symlink that is just the name of the profile: `<profile-name>`.
The target of the symlink is *not* a store path, but instead the name of a generation symlink (which is a relative path to that symlink within the current directory).

Each of these symlinks is a root for the Nix garbage collector.

### Filesystem layout

Here is an example of a profile:

```console
$ ls -l ~alice/.local/state/nix/profiles/profile*
lrwxrwxrwx 1 alice users 14 Nov 25 14:35 /home/alice/.local/state/nix/profiles/profile -> profile-7-link
lrwxrwxrwx 1 alice users 51 Oct 28 16:18 /home/alice/.local/state/nix/profiles/profile-5-link -> /nix/store/q69xad13ghpf7ir87h0b2gd28lafjj1j-profile
lrwxrwxrwx 1 alice users 51 Oct 29 13:20 /home/alice/.local/state/nix/profiles/profile-6-link -> /nix/store/6bvhpysd7vwz7k3b0pndn7ifi5xr32dg-profile
lrwxrwxrwx 1 alice users 51 Nov 25 14:35 /home/alice/.local/state/nix/profiles/profile-7-link -> /nix/store/mp0x6xnsg0b8qhswy6riqvimai4gm677-profile
```

- `/home/alice/.local/state/nix/profiles` is the directory that contains the profile.

- `profile` is the name of the profile.

- Generations 5, 6, and 7, currently exist.
  (Generations 1 to 4 once existed but were deleted.)

- Generation 7 is the active generation.

## Symlink tree profile

Typical profiles created by [`nix-env`](@docroot@/command-ref/nix-env.md) (and indirectly by [`nix-channel`](@docroot@/command-ref/nix-channel.md) too) have additional structure.
The contents of each generation is not just can arbitrary store object, but a symlink tree merging together various other store objects.

Additionally, the store object contains a manifest file, providing some info on what those merged store objects are and where they came from:
- [`manifest.nix`](@docroot@/command-ref/files/manifest.nix.md) used by [`nix-env`](@docroot@/command-ref/nix-env.md).
- [`manifest.json`](@docroot@/command-ref/files/manifest.json.md) used by [`nix profile`](@docroot@/command-ref/new-cli/nix3-profile.md) (experimental).

### Filesystem layout

Here is an example of a `nix-env` profile:

```console
$ ll -R ~eelco/.local/state/nix/profiles/profile-7-link/
/home/eelco/.local/state/nix/profiles/profile-7-link/:
total 20
dr-xr-xr-x 2 root root 4096 Jan  1  1970 bin
-r--r--r-- 2 root root 1402 Jan  1  1970 manifest.nix
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

## User profile link

A symbolic link to the user's current profile:

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
