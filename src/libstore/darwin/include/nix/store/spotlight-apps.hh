#pragma once
/**
 * @file
 *
 * Hybrid `.app` bundle materialization for macOS Spotlight integration.
 *
 * Background
 * ==========
 *
 * The Nix store on macOS lives on a dedicated APFS volume mounted with the
 * `nobrowse` flag (see `scripts/create-darwin-volume.sh`). The flag hides the
 * volume from Finder and, more importantly for this code, *prevents the
 * volume from carrying its own Spotlight metadata index*: any attempt to
 * `mdutil -i on /nix` is rejected with `MDConfigCreateStore` error `-400`,
 * because Apple's metadata configuration code refuses to create a per-volume
 * `.Spotlight-V100` directory on `nobrowse` APFS volumes.
 *
 * The downstream effect is well-known: after `nix profile install foo`,
 * `foo.app` lives somewhere under `/nix/store/.../Applications/foo.app`,
 * Launch Services *can* be told about it (via `LSRegisterURL`) and `open -a
 * foo` works, but the Spotlight UI never surfaces it under "Applications" -
 * because the Spotlight UI is populated from `mds`'s file index, not from
 * Launch Services. With no index store on `/nix`, there is nothing for
 * `mdimport` to write into, and so the bundle never appears in ⌘-Space.
 *
 * What does work
 * ==============
 *
 * Apple's `Application.mdimporter` only ever reads two things from a `.app`
 * bundle to produce a complete Spotlight record:
 *
 *   - `Contents/Info.plist`  - for `kMDItemCFBundleIdentifier`,
 *                              `kMDItemDisplayName`, `kMDItemKind = "Application"`,
 *                              etc. (34 attributes total in our experiments)
 *   - The icon file named by `CFBundleIconFile` - used to render the result
 *     in the Spotlight UI.
 *
 * Everything else under `Contents/` (the `MacOS/` executable, `Frameworks/`,
 * `_CodeSignature/`, the rest of `Resources/`, ...) is irrelevant to the
 * importer. The importer reads files via the normal POSIX path, and the
 * `nobrowse` flag does *not* block reads - it only blocks indexing.
 *
 * That means we can give Spotlight what it needs by materializing a *hybrid*
 * `.app` bundle on the indexed boot volume:
 *
 *   1. A real directory at e.g. `~/Applications/Nix Apps/foo.app/Contents/`
 *   2. A real, byte-identical *copy* of `Contents/Info.plist` from the store
 *   3. A real copy of the icon file referenced by `CFBundleIconFile`
 *   4. Symlinks for everything else under `Contents/` and `Contents/Resources/`
 *      pointing back into the original `/nix/store/.../foo.app/Contents/...`
 *   5. A `LSRegisterURL` call so Launch Services picks it up immediately
 *   6. An `mdimport -i` call so Spotlight indexes it now instead of on its
 *      next periodic walk
 *
 * The store remains authoritative for every byte that actually executes; the
 * only bytes that leave the store are `Info.plist` and the icon (a few
 * kilobytes per app). Garbage collection is unaffected - the materialized
 * directory is *not* a GC root, the profile generation symlinks still are.
 *
 * Why not other approaches
 * ========================
 *
 * - Plain symlinks in `~/Applications` - Apple's `Application.mdimporter`
 *   does not follow symlinks across volume boundaries during indexing, so the
 *   `Info.plist` it would need is on the unindexable `/nix` volume.
 *
 * - Finder alias files (`osascript ... make alias file ...`) - these create a
 *   real file with UTI `com.apple.alias-file`, which `mdimport` indexes as
 *   "an alias file", not as `com.apple.application-bundle`. They never appear
 *   in Spotlight's Applications category. Confirmed empirically on macOS 26.
 *
 * - `LSRegisterURL` alone on the store path - works at the Launch
 *   Services layer (`open -a foo` succeeds), but the Spotlight UI's
 *   Applications results come from `mds`, not from LS. LS knowing isn't
 *   enough.
 *
 * - `CSSearchableIndex.indexSearchableItems:` - wrong pipeline entirely.
 *   That API funnels through `+[CSInlineDonation _inlineDonationWithOverrideBundleID:...]`,
 *   gated by `+[CSInlineDonation isInlineCascadeDonationEnabled]`, which on
 *   macOS 13+ is compiled to a literal `mov w0, #0; ret`. Even bypassing the
 *   gate (via Obj-C method swizzling, which we verified end-to-end), the
 *   donation lands in `BiomeCascade` as an `AppIntentsIndexedEntity` - that
 *   is the AppIntents/Siri Suggestions pipeline, *not* the launchable-app
 *   pipeline. Items donated this way appear (if at all) as suggestions from
 *   the donor app, never as ⏎-launchable bundles.
 *
 * - Copy the whole bundle - defeats the point of the store (deduplication
 *   between profile generations, garbage collection of old apps, atomic
 *   rollback). The hybrid copy keeps the executable in the store.
 *
 * Layout
 * ======
 *
 * Materialized bundles live under a per-user directory we own outright:
 *
 *   - root profile  -> `/Applications/Nix Profile Apps/`
 *   - user profile  -> `$HOME/Applications/Nix Profile Apps/`
 *
 * The directory name is deliberately distinct from
 *
 *   - `nix-darwin`'s `/Applications/Nix Apps/` (system-wide module activation)
 *   - `home-manager`'s `$HOME/Applications/Home Manager Apps/` (HM activation)
 *
 * so that Nix's profile activation never has to reason about, or contend
 * with, those tools' bookkeeping. We blow our directory away and rebuild it
 * on every profile switch. This is idempotent, fast (handful of small file
 * copies plus symlinks per app), and avoids any need for cross-invocation
 * state.
 *
 * Safety
 * ======
 *
 * `syncProfileAppBundles` is `noexcept`. It is best-effort: any error during
 * materialization is logged at `warn` level and the function returns. A
 * failure here must never abort a profile switch - Spotlight integration is
 * a convenience, not a correctness requirement.
 *
 * This header is only on the include path for Darwin builds (it lives
 * under `src/libstore/darwin/include`); callers must guard the `#include`
 * itself with `#ifdef __APPLE__`.
 *
 * See also
 * ========
 *
 *   - `src/libstore/darwin/spotlight-apps.cc` - implementation
 *   - `doc/manual/source/installation/macos-spotlight-apps.md` - user-facing
 *     documentation
 *   - `scripts/create-darwin-volume.sh` - where the `nobrowse` mount flag is
 *     set, which is the original cause of the problem
 *   - NixOS/nix#7055 - the issue this addresses
 */

#include <filesystem>

namespace nix::darwin {

/**
 * Synchronize the hybrid Spotlight app-bundle directory for the given
 * profile.
 *
 * @param profile  Path to a profile (e.g. `~/.nix-profile`,
 *                 `/nix/var/nix/profiles/default`, or any generation symlink).
 *                 Must already point at the desired generation when this
 *                 function is called - typically called immediately after
 *                 `replaceSymlink()` in `switchLink()`.
 *
 * The function:
 *   1. Resolves `profile` and looks for `<resolved>/Applications/`.
 *      If absent, it removes our destination directory entirely (so apps
 *      that were uninstalled disappear from Spotlight) and returns.
 *   2. Wipes our destination directory.
 *   3. For each `*.app` under the source `Applications/`, materializes a
 *      hybrid bundle in the destination (Info.plist + icon copied, every
 *      other file in `Contents/` symlinked back into the store).
 *   4. Calls `LSRegisterURL` on each new bundle so Launch Services sees it.
 *   5. Spawns `mdimport -i` on the destination directory to ask Spotlight
 *      to index the new bundles immediately.
 *
 * No exceptions escape this function. Failures are logged via Nix's
 * `warn(...)` and the operation continues with the next bundle where
 * sensible.
 */
void syncProfileAppBundles(const std::filesystem::path & profile) noexcept;

namespace detail {

/**
 * Test seam for `syncProfileAppBundles`. Materializes hybrid bundles from
 * `profile` into the explicit destination directory `dest`, optionally
 * skipping the system-wide side effects (`LSRegisterURL` and the
 * `/usr/bin/mdimport -i` spawn).
 *
 * Production code never calls this directly - `syncProfileAppBundles`
 * dispatches here with `dest` derived from the calling user's home and
 * `notifySystem = true`. The unit tests call it with a tempdir and
 * `notifySystem = false` so they can exercise the file ops without
 * mutating Launch Services or kicking the real Spotlight indexer.
 *
 * Same `noexcept`/best-effort contract as the public entry point.
 */
void syncProfileAppBundlesAt(
    const std::filesystem::path & profile, const std::filesystem::path & dest, bool notifySystem) noexcept;

} // namespace detail

} // namespace nix::darwin
