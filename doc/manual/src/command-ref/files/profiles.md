## Profiles

Profiles are a rudimentary mechanism for version control.
Versions of a profile are also called *generations*.

## Profiles directory

A directory managed by [`nix-env`] and [`nix profile`]\ (experimental) that contains symlinks to files in the [Nix store](@docroot@/glossary.md#gloss-store):

- `$XDG_STATE_HOME/nix/profiles` for regular users
- `$NIX_STATE_DIR/profiles/per-user/root` if the user is `root`

## Profile contents

For a profile named *path*, the entry *path* in the [profiles directory](#profiles-directory) is a symlink to *path*`-`*N*`-link`, where *N* is the version of the profile.
In turn, *path*`-`*N*`-link` is a symlink to a [store path](@docroot@/glossary.md#gloss-store-path).

Each of these symlinks is a root for the Nix [garbage collector](@docroot@/command-ref/nix-store/gc.md).

Each profile version contains a manifest file:
- [`manifest.nix`](@docroot@/command-ref/files/manifest.nix.md) used by [`nix-env`]
- [`manifest.json`](@docroot@/command-ref/files/manifest.json.md) used by [`nix profile`]\ (experimental)

> **Example**
>
> The default profile of user `alice` points to generation 7.
> The oldest generation available is 5.
>
> ```console
> $ ls -l ~alice/.local/state/nix/profiles/profile*
> lrwxrwxrwx 1 alice users 14 Nov 25 14:35 /home/alice/.local/state/nix/profiles/profile -> profile-7-link
> lrwxrwxrwx 1 alice users 51 Oct 28 16:18 /home/alice/.local/state/nix/profiles/profile-5-link -> /nix/store/q69xad13ghpf7ir87h0b2gd28lafjj1j-profile
> lrwxrwxrwx 1 alice users 51 Oct 29 13:20 /home/alice/.local/state/nix/profiles/profile-6-link -> /nix/store/6bvhpysd7vwz7k3b0pndn7ifi5xr32dg-profile
> lrwxrwxrwx 1 alice users 51 Nov 25 14:35 /home/alice/.local/state/nix/profiles/profile-7-link -> /nix/store/mp0x6xnsg0b8qhswy6riqvimai4gm677-profile
> ```

## User profile link

A symbolic link to the user's current profile:

- `~/.nix-profile`
- `$XDG_STATE_HOME/nix/profile` if [`use-xdg-base-directories`] is set to `true`.

By default, this symlink points to:

- `$XDG_STATE_HOME/nix/profiles/profile` for regular users
- `$NIX_STATE_DIR/profiles/per-user/root/profile` for `root`

The `PATH` environment variable should include the `/bin` subdirectory of the profile link (e.g. `~/.nix-profile/bin`) for executables in that profile to be exposed to the user.
The [Nix installer](@docroot@/installation/installing-binary.md) sets this up by default, unless you enable [`use-xdg-base-directories`].

[`nix profile`]: @docroot@/command-ref/new-cli/nix3-profile.md
[`nix-env`]: @docroot@/command-ref/nix-env.md
[`use-xdg-base-directories`]: @docroot@/command-ref/conf-file.md#conf-use-xdg-base-directories
