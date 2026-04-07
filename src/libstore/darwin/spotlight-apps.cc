/**
 * @file
 *
 * Hybrid macOS `.app` bundle materializer. See `spotlight-apps.hh` for the
 * rationale and the list of approaches tried and rejected.
 */

#include "nix/store/spotlight-apps.hh"

#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/processes.hh"
#include "nix/util/users.hh"
#include "nix/util/util.hh"

#include <sys/stat.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

namespace nix::darwin {

namespace {

namespace fs = std::filesystem;

/**
 * Small RAII wrapper for CoreFoundation objects. Releases on destruction
 * and forbids copies; transfers ownership on move.
 */
template<typename T>
class CFRef
{
    T ref;

public:
    CFRef()
        : ref(nullptr)
    {
    }

    explicit CFRef(T r)
        : ref(r)
    {
    }

    CFRef(const CFRef &) = delete;
    CFRef & operator=(const CFRef &) = delete;

    CFRef(CFRef && o) noexcept
        : ref(o.ref)
    {
        o.ref = nullptr;
    }

    CFRef & operator=(CFRef && o) noexcept
    {
        if (this != &o) {
            if (ref)
                CFRelease(ref);
            ref = o.ref;
            o.ref = nullptr;
        }
        return *this;
    }

    ~CFRef()
    {
        if (ref)
            CFRelease(ref);
    }

    T get() const
    {
        return ref;
    }

    explicit operator bool() const
    {
        return ref != nullptr;
    }
};

/**
 * Convert a UTF-8 `std::string_view` into a freshly-allocated `CFStringRef`.
 */
static CFRef<CFStringRef> cfStr(std::string_view s)
{
    return CFRef<CFStringRef>(CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(s.data()), s.size(), kCFStringEncodingUTF8, false));
}

/**
 * Read an `Info.plist` (binary or XML - `CFPropertyListCreateWithData`
 * handles both transparently) and return it as a `CFDictionaryRef`. Returns
 * an empty `CFRef` on any failure.
 */
static CFRef<CFDictionaryRef> readInfoPlist(const fs::path & infoPlist)
{
    std::string buf;
    try {
        buf = readFile(infoPlist);
    } catch (SystemError &) {
        return {};
    }
    if (buf.empty())
        return {};

    CFRef<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(buf.data()), buf.size()));
    if (!data)
        return {};

    CFRef<CFPropertyListRef> plist(
        CFPropertyListCreateWithData(kCFAllocatorDefault, data.get(), kCFPropertyListImmutable, nullptr, nullptr));
    if (!plist || CFGetTypeID(plist.get()) != CFDictionaryGetTypeID())
        return {};

    /* We are about to drop `plist`, but we want to keep the dict alive - so
       retain it explicitly and hand the retained ref to the returned wrapper. */
    auto dict = static_cast<CFDictionaryRef>(plist.get());
    CFRetain(dict);
    return CFRef<CFDictionaryRef>(dict);
}

/**
 * Pull a UTF-8 `std::string` out of a `CFDictionary` entry, if present and
 * actually a `CFString`. Returns `std::nullopt` otherwise.
 */
static std::optional<std::string> dictString(CFDictionaryRef dict, CFStringRef key)
{
    if (!dict)
        return std::nullopt;
    CFTypeRef raw = CFDictionaryGetValue(dict, key);
    if (!raw || CFGetTypeID(raw) != CFStringGetTypeID())
        return std::nullopt;
    auto s = static_cast<CFStringRef>(raw);

    /* `CFStringGetMaximumSizeForEncoding` gives an upper bound; round-trip
       via a heap buffer. */
    CFIndex len = CFStringGetLength(s);
    CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string out(maxBytes, '\0');
    if (!CFStringGetCString(s, out.data(), maxBytes, kCFStringEncodingUTF8))
        return std::nullopt;
    out.resize(std::strlen(out.c_str()));
    return out;
}

/**
 * Resolve a `CFBundleIconFile` value to its actual filename in
 * `Contents/Resources/`. Apple allows the value to be specified with or
 * without the `.icns` extension; we add it if missing. Some bundles also
 * store icons under a different extension (e.g. `.png`); we try a few common
 * alternatives if the canonical `.icns` does not exist.
 */
static std::optional<fs::path> findIconFile(const fs::path & resourcesDir, const std::string & iconKey)
{
    if (iconKey.empty())
        return std::nullopt;

    fs::path direct = resourcesDir / iconKey;
    if (pathExists(direct))
        return direct;

    if (!fs::path(iconKey).has_extension()) {
        for (const char * ext : {".icns", ".png", ".tiff", ".jpg"}) {
            fs::path withExt = resourcesDir / (iconKey + ext);
            if (pathExists(withExt))
                return withExt;
        }
    }

    return std::nullopt;
}

/**
 * Materialize one hybrid `.app` bundle.
 *
 * @param srcApp  Canonical store path of the source `.app` (already
 *                symlink-resolved by the caller).
 * @param destApp Destination directory
 *                (e.g. `~/Applications/Nix Profile Apps/foo.app`).
 *
 * @return `true` on success, `false` if anything went sufficiently wrong
 * that the destination should be considered unusable. The caller logs and
 * continues either way.
 */
static bool materializeHybridBundle(const fs::path & srcApp, const fs::path & destApp, bool notifySystem)
{
    fs::path srcContents = srcApp / "Contents";
    fs::path srcInfoPlist = srcContents / "Info.plist";

    /* Not actually a bundle (e.g. a stray file with `.app` suffix). */
    auto contentsSt = maybeLstat(srcContents);
    if (!contentsSt || !S_ISDIR(contentsSt->st_mode))
        return false;

    /* No Info.plist -> can't be made into something Spotlight will index
       as an Application. Skip silently. */
    auto plistSt = maybeLstat(srcInfoPlist);
    if (!plistSt || !S_ISREG(plistSt->st_mode))
        return false;

    /* Read CFBundleIconFile so we know what icon to copy. */
    auto plist = readInfoPlist(srcInfoPlist);
    std::optional<std::string> iconKey;
    if (plist) {
        auto k = cfStr("CFBundleIconFile");
        iconKey = dictString(plist.get(), k.get());
    }

    fs::path destContents = destApp / "Contents";
    fs::path destResources = destContents / "Resources";
    createDirs(destResources);

    /* Copy Info.plist (mandatory). */
    copyFile(srcInfoPlist, destContents / "Info.plist", /*andDelete=*/false, /*contents=*/true);

    /* Copy PkgInfo if present (optional, harmless if missing). */
    {
        fs::path pkgInfo = srcContents / "PkgInfo";
        if (auto st = maybeLstat(pkgInfo); st && S_ISREG(st->st_mode))
            copyFile(pkgInfo, destContents / "PkgInfo", /*andDelete=*/false, /*contents=*/true);
    }

    /* Copy the icon file (optional but strongly preferred - without it the
       Spotlight result has a blank icon). */
    fs::path srcResources = srcContents / "Resources";
    std::optional<fs::path> copiedIconFile;
    if (iconKey && !iconKey->empty()) {
        if (auto iconPath = findIconFile(srcResources, *iconKey)) {
            /* `findIconFile` only ever returns a direct child of
               `srcResources`. The basename-only filter below depends on
               that invariant. */
            assert(iconPath->parent_path() == srcResources);
            fs::path destIcon = destResources / iconPath->filename();
            try {
                copyFile(*iconPath, destIcon, /*andDelete=*/false, /*contents=*/true);
                copiedIconFile = iconPath->filename();
            } catch (SystemError &) {
                /* No icon -> blank icon in Spotlight, but bundle is still
                   indexable. Continue. */
            }
        }
    }

    /* Symlink everything else under Contents/ into the store. */
    try {
        for (auto & entry : DirectoryIterator{srcContents}) {
            const auto & name = entry.path().filename();
            if (name == "Info.plist" || name == "PkgInfo" || name == "Resources")
                continue;
            replaceSymlink(entry.path(), destContents / name);
        }
    } catch (SystemError &) {
        return false;
    }

    /* Symlink everything else under Resources/ into the store, except the
       icon file we copied above. */
    if (auto st = maybeLstat(srcResources); st && S_ISDIR(st->st_mode)) {
        try {
            for (auto & entry : DirectoryIterator{srcResources}) {
                const auto & name = entry.path().filename();
                if (copiedIconFile && name == *copiedIconFile)
                    continue;
                replaceSymlink(entry.path(), destResources / name);
            }
        } catch (SystemError & e) {
            warn(
                "could not fully enumerate %s while materializing %s: %s "
                "(some resource symlinks may be missing - Spotlight will "
                "still index the bundle but launching may be impaired)",
                PathFmt(srcResources),
                PathFmt(destApp),
                e.what());
        }
    }

    /* Make the bundle resolvable via `open -a Foo` immediately, before
       `mds` finishes indexing. `inUpdate = true` replaces any prior
       registration at the same path. */
    if (notifySystem) {
        auto destStr = destApp.string();
        CFRef<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(destStr.data()), destStr.size(), true));
        if (url)
            (void) LSRegisterURL(url.get(), true);
    }

    return true;
}

/**
 * Determine where we should materialize bundles for the calling user.
 *
 * We deliberately use a destination path that no other Nix-on-macOS tool
 * touches, so we don't have to coordinate with `nix-darwin`'s
 * `system.activationScripts.applications` or `home-manager`'s
 * `home.activation.aliasApplications`.
 */
static fs::path destinationDir()
{
    if (isRootUser())
        return "/Applications/Nix Profile Apps";

    return getHome() / "Applications" / "Nix Profile Apps";
}

/**
 * Walk a profile target's `Applications/` directory and return the
 * absolute, symlink-resolved paths of every `.app` bundle inside (one
 * level deep - matching how `buildenv` lays them out).
 */
static std::vector<fs::path> findAppBundles(const fs::path & profileTarget)
{
    std::vector<fs::path> out;
    fs::path appsDir = profileTarget / "Applications";

    auto st = maybeLstat(appsDir);
    if (!st || !S_ISDIR(st->st_mode))
        return out;

    try {
        for (auto & entry : DirectoryIterator{appsDir}) {
            if (entry.path().extension() != ".app")
                continue;
            /* Resolve through the buildenv symlink chain so that destApp's
               symlinks point at the canonical store path, not at another
               symlink farm. This keeps the materialized bundle valid even if
               the intermediate generation directories are GC'd later (the
               canonical store path is what the profile pins as a GC root). */
            try {
                out.push_back(canonPath(entry.path(), /*resolveSymlinks=*/true));
            } catch (SystemError &) {
                /* Dangling or unresolvable - skip. */
            }
        }
    } catch (SystemError & e) {
        warn("could not enumerate %s for Spotlight indexing: %s", PathFmt(appsDir), e.what());
    }

    return out;
}

} // anonymous namespace

namespace detail {

void syncProfileAppBundlesAt(const fs::path & profile, const fs::path & dest, bool notifySystem) noexcept
{
    try {
        /* Resolve the profile symlink (e.g. `~/.nix-profile`) to the actual
           generation directory it currently points at. If the profile does
           not exist or does not resolve, treat that as "no apps" and let
           the cleanup branch below remove our destination. */
        fs::path target;
        if (pathExists(profile)) {
            try {
                target = canonPath(profile, /*resolveSymlinks=*/true);
            } catch (SystemError &) {
                /* Treat as "no apps" - cleanup branch below removes dest. */
            }
        }

        std::vector<fs::path> bundles;
        if (!target.empty())
            bundles = findAppBundles(target);

        /* If there are no apps in the new profile, just remove our
           destination directory and we're done. This is the only path by
           which an *uninstall* propagates to Spotlight: the next profile
           switch sees fewer apps and rebuilds the destination accordingly. */
        if (bundles.empty()) {
            deletePath(dest);
            return;
        }

        /* Wipe + rebuild. We own this directory outright; nothing else on
           the system writes to "Nix Profile Apps". This is the simplest
           possible state model and avoids any need to track which bundles
           we created on a previous run. */
        deletePath(dest);
        createDirs(dest);

        size_t ok = 0;
        for (const auto & srcApp : bundles) {
            fs::path destApp = dest / srcApp.filename();
            try {
                if (materializeHybridBundle(srcApp, destApp, notifySystem))
                    ok++;
            } catch (std::exception & e) {
                warn("could not materialize hybrid app bundle for %s: %s", PathFmt(srcApp), e.what());
            }
        }

        /* Nudge Spotlight to index the new bundles now instead of waiting
           for its next periodic walk. Best-effort: runProgram returns the
           exit status (we discard it) and only throws on more fundamental
           failures like spawn errors, which we swallow here. */
        if (notifySystem && ok > 0) {
            try {
                (void) runProgram(
                    RunOptions{
                        .program = "/usr/bin/mdimport",
                        .lookupPath = false,
                        .args = {"-i", dest.string()},
                        .mergeStderrToStdout = true,
                    });
            } catch (std::exception &) {
                /* best-effort */
            }
        }

        debug("syncProfileAppBundles: materialized %d/%d bundles into %s", ok, bundles.size(), PathFmt(dest));
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

} // namespace detail

void syncProfileAppBundles(const fs::path & profile) noexcept
{
    detail::syncProfileAppBundlesAt(profile, destinationDir(), /* notifySystem = */ true);
}

} // namespace nix::darwin
