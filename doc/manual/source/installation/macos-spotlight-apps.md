# macOS Spotlight Integration

On macOS, Nix makes profile-installed `.app` bundles visible to Spotlight, `mdfind`, and `open -a <app-name>`.

When a profile generation contains an `Applications/` directory, Nix creates one shadow bundle per app under a profile-specific subdirectory of:

| Command user | Destination directory              |
| ------------ | ---------------------------------- |
| Non-root     | `~/Applications/Nix Profile Apps/` |
| Root         | `/Applications/Nix Profile Apps/`  |

The shadow is a small hybrid bundle.
`Contents/Info.plist`, `Contents/PkgInfo` if present, and the icon named by `CFBundleIconFile` are copied onto the boot volume.
Other entries under `Contents/` and `Contents/Resources/` are symlinks back into the original store path.

For example:

```console
~/Applications/Nix Profile Apps/profile-.../Foo.app/
└── Contents/
    ├── Info.plist
    ├── PkgInfo
    ├── MacOS -> /nix/store/.../Foo.app/Contents/MacOS
    ├── Frameworks -> /nix/store/.../Foo.app/Contents/Frameworks
    └── Resources/
        ├── AppIcon.icns
        └── en.lproj -> /nix/store/.../Foo.app/Contents/Resources/en.lproj
```

The executable, frameworks, and other runtime resources stay in `/nix/store`.
The shadow directory is not a garbage-collection root.
The profile generation still owns the store references.

## Why this is needed

Nix's macOS store volume is mounted with `nobrowse`.
That keeps the volume out of Finder, but it also prevents Spotlight from creating an index store on `/nix`.
Even as root, `mdutil -i on /nix` fails with error `-400` and leaves `/nix` in an unknown indexing state.

Plain symlinks from `~/Applications` into `/nix/store` are not enough, because Spotlight's filesystem walk does not follow them across the volume boundary.
Launch Services registration alone is not enough either: `open -a` may work while Spotlight still has no application-bundle record.

The hybrid bundle gives Apple's application importer a real bundle directory, real bundle metadata, and a real icon on an indexed volume.
The launch path can remain symlinked into the store.

## When it runs

This runs after profile switches made by [`nix profile`] operations (`add`, `remove`, `upgrade`, `rollback`) and the corresponding [`nix-env`] operations.
Nix removes and recreates that profile's subdirectory each time, then asks Launch Services and Spotlight to pick up the new bundles.

Failures are warnings only.
A Spotlight integration error never aborts a profile switch.

## Verifying

After installing a package that contains an application bundle:

```console
$ app=Foo
$ ls -la "$HOME/Applications/Nix Profile Apps/"
$ mdfind -onlyin "$HOME/Applications/Nix Profile Apps/" 'kMDItemKind == "Application"'
$ appPath=$(mdfind -onlyin "$HOME/Applications/Nix Profile Apps/" "kMDItemFSName == '$app.app'" | head -n1)
$ mdls "$appPath" \
    | grep -E 'kMDItemKind|kMDItemCFBundleIdentifier|kMDItemContentType '
$ open -a "$app"
```

Spotlight indexing is asynchronous, so `mdfind` and `mdls` may take a few seconds to show a newly installed app.

If `mdfind` sees the app but Spotlight search does not, check that Spotlight is enabled on the boot volume:

```console
$ mdutil -s /
```

If needed, re-enable it with:

```console
$ sudo mdutil -i on /
```

The `/nix` volume does not need indexing; the `nobrowse` volume rejects it.

## Other macOS app managers

This integration writes only to `Nix Profile Apps`, so it does not manage or delete `nix-darwin`'s `/Applications/Nix Apps/` or Home Manager's `~/Applications/Home Manager Apps/`.

[`nix profile`]: @docroot@/command-ref/new-cli/nix3-profile.md
[`nix-env`]: @docroot@/command-ref/nix-env.md
