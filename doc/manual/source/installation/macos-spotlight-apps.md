# macOS Spotlight Integration

On macOS, Nix automatically integrates `.app` bundles installed through user profiles with the system Spotlight search and Launch Services.
After running `nix profile install` for a package with an application bundle, the app appears in Spotlight (⌘-Space), `mdfind`, and as a target for `open -a <appname>`."

This page explains how that works,
why every byte that runs at launch time still comes from `/nix/store`,
and what to do if something goes wrong.

## Where it lives

When Nix switches a profile generation that contains an `Applications/` directory,
it materializes a *hybrid* shadow of every `.app` it finds into:

| Profile owner | Destination directory |
|---|---|
| User profile (e.g. `~/.nix-profile`) | `~/Applications/Nix Profile Apps/` |
| Root profile  (e.g. `/nix/var/nix/profiles/default`) | `/Applications/Nix Profile Apps/` |

The destination directory name is deliberately distinct from `Nix Apps` (used by `nix-darwin`'s `system.activationScripts.applications`) and from `Home Manager Apps` (used by `home-manager`'s `home.activation.aliasApplications`),
so Nix never has to coordinate with those tools' bookkeeping.
The `Nix Profile Apps` directory is owned by Nix end-to-end:
it is wiped and recreated on every profile switch.

## What is in each shadow bundle

For a source bundle at `/nix/store/<hash>-foo-1.2.3/Applications/Foo.app`, Nix produces:

```
~/Applications/Nix Profile Apps/Foo.app/
├── Contents/
│   ├── Info.plist      <- real copy from the store (small)
│   ├── PkgInfo         <- real copy if it exists (8 bytes)
│   ├── MacOS           -> /nix/store/<hash>-foo-1.2.3/Applications/Foo.app/Contents/MacOS
│   ├── Frameworks      -> /nix/store/<hash>-foo-1.2.3/Applications/Foo.app/Contents/Frameworks
│   ├── _CodeSignature  -> /nix/store/<hash>-foo-1.2.3/Applications/Foo.app/Contents/_CodeSignature
│   └── Resources/
│       ├── AppIcon.icns   <- real copy of the icon referenced by CFBundleIconFile
│       ├── en.lproj       -> /nix/store/.../Resources/en.lproj
│       └── ...            -> /nix/store/.../Resources/...
```

Two files are copied; everything else is a symlink back into the store.
The real executable, frameworks, and resources never leave `/nix/store`.
Garbage collection is unaffected:
the shadow directory is *not* a GC root,
and the profile generation symlinks remain authoritative.

## Why exactly these two files are copied

Apple's `Application.mdimporter` (the Spotlight importer plug-in for app bundles) reads only two things from a `.app` to populate its Spotlight record:

1. `Contents/Info.plist` - for `kMDItemCFBundleIdentifier`, `kMDItemDisplayName`, `kMDItemKind = "Application"`, the localized name, the version string, and ~30 other attributes.
2. The icon file named by the `CFBundleIconFile` key - used to render the result row in the Spotlight UI and in Launch Services.

Everything else under `Contents/` (`MacOS/`, `Frameworks/`, `_CodeSignature/`, the rest of `Resources/`, ...) is opened only when Launch Services actually launches the app,
which happens via the normal POSIX path that follows symlinks transparently.
Spotlight never reads it during indexing.

So if `Info.plist` and the icon file live on the indexed boot volume, the importer is happy and the app shows up in Spotlight.
The remainder can be symlinks into the unindexable `/nix` volume - Spotlight does not care, and launching still works.

## Why the symlinks have to be hybrid (the `nobrowse` story)

Nix on macOS installs the store on a dedicated APFS volume mounted at `/nix`.
The volume is mounted with `nobrowse` (and `noauto`, `nosuid`, `noatime`, `owners`) - see `scripts/create-darwin-volume.sh`.
The `nobrowse` flag has two effects relevant to Spotlight:

1. Finder hides the volume from the desktop and the sidebar.
   (This is the reason the flag exists.)
2. Apple's metadata configuration code refuses to create a per-volume Spotlight index store on the volume.
   Running `sudo mdutil -i on /nix` fails with `MDConfigCreateStore` error `-400`,
   which is APFS-mount-flag gating, not a permissions or disk-space problem.

A consequence of #2: there is no `.Spotlight-V100` directory on `/nix`,
so even though `Application.mdimporter` *can* read store-path bundles (`mdimport -t -d3` on a store path produces a complete 34-attribute record),
its output gets thrown away because there is no destination index.
Indexing just the boot volume is also not enough:
Apple's Spotlight indexer explicitly does not follow symlinks across volume boundaries during its filesystem walk,
so a plain symlink in `~/Applications` pointing into the store is never picked up by the importer.

The fix is to give the importer a path that does not cross a volume boundary:
a real `.app` directory rooted on the boot volume, with the `Info.plist` and icon physically present on that volume.
The remainder can be symlinks because the importer never touches them.

## When does this run

The materialization runs from every profile-switch path:
`nix profile install`/`remove`/`upgrade`/`rollback`, and the corresponding `nix-env` operations.
After replacing the profile symlink,
Nix wipes the `Nix Profile Apps` directory,
materializes one hybrid bundle per `.app` in the new generation,
registers each one with Launch Services,
and runs `/usr/bin/mdimport -i <dest>` so Spotlight picks them up immediately.

Spotlight integration is best-effort:
any failure during materialization is logged as a warning and the profile switch continues normally.

## Verifying it works

After a `nix profile install` of a package that ships a `.app` bundle:

```sh
# 1. The shadow bundle should exist:
ls -la ~/Applications/Nix\ Profile\ Apps/

# 2. mdfind should see it (this hits Apple's mds index, not LS):
mdfind -onlyin ~/Applications/Nix\ Profile\ Apps/ 'kMDItemKind == "Application"'

# 3. mdls should report the right bundle identifier and a content type
#    of com.apple.application-bundle:
mdls ~/Applications/Nix\ Profile\ Apps/<App>.app | grep -E 'kMDItemKind|kMDItemCFBundleIdentifier|kMDItemContentType '

# 4. open -a should resolve via Launch Services and run the real store
#    binary through the symlink chain:
open -a <App>
```

In the GUI: hit ⌘-Space and start typing the app name.
It should appear under "Applications" with its real icon, and ⏎ should launch it.

If it does *not* appear in the GUI but `mdfind` does see it,
the most likely cause is that Spotlight is disabled on the boot volume.
Check with:

```sh
mdutil -s /
```

If this says `Indexing disabled`, re-enable it with:

```sh
sudo mdutil -i on /
```

(This will trigger a background reindex of `/`.
Do not enable it on `/nix` - that will fail with `-400` regardless, see above.)

## Disabling

The integration runs unconditionally on macOS,
since it is best-effort and costs at most a few milliseconds plus a few kilobytes per app.
If you need to opt out (for example, because you are using `nix-darwin` and prefer to have all of your apps in `/Applications/Nix Apps/` only),
simply do not run profile commands as the user - `nix-darwin`'s system activation does not go through `switchLink()` and is unaffected.

To remove all materialized bundles for the current user,
switch to an empty profile and switch back,
or just delete the directory:

```sh
rm -rf ~/Applications/Nix\ Profile\ Apps
```

The next profile switch will rebuild it.

## Interaction with `nix-darwin` and `home-manager`

This integration **does not interfere with `nix-darwin`'s `system.activationScripts.applications`** or with `home-manager`'s `home.activation.aliasApplications`:

- `nix-darwin` writes to `/Applications/Nix Apps/`.
  Nix writes to `~/Applications/Nix Profile Apps/` (or `/Applications/Nix Profile Apps/` for root profiles).
  The directories are distinct.
- `home-manager` writes to `~/Applications/Home Manager Apps/`.
  Same - a different directory.

If you use both `nix-darwin` and `nix profile install` at the user level,
you can end up with the same app appearing twice in Spotlight:
once from `/Applications/Nix Apps/` (system activation) and once from `~/Applications/Nix Profile Apps/` (user profile activation).
This is expected - Spotlight will deduplicate based on bundle identifier in most cases,
but if both shadows have slightly different versions you will see both.
Remove the user-level install if you do not want the duplicate.

## Internals reference

| Component                                   | Location                                                        |
|---------------------------------------------|-----------------------------------------------------------------|
| Public header                               | `src/libstore/darwin/include/nix/store/spotlight-apps.hh`       |
| Implementation                              | `src/libstore/darwin/spotlight-apps.cc`                         |
| Hook site                                   | `src/libstore/profiles.cc`, `nix::switchLink()`                 |
| Build wiring                                | `src/libstore/darwin/meson.build`, `src/libstore/meson.build`   |
| Volume creation (where `nobrowse` is set)   | `scripts/create-darwin-volume.sh`                               |
| Original issue                              | [NixOS/nix#7055](https://github.com/NixOS/nix/issues/7055)      |
