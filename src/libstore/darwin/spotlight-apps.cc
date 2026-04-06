/**
 * @file
 *
 * Hybrid macOS `.app` bundle materializer. See `spotlight-apps.hh` for the
 * rationale and the list of approaches tried and rejected.
 */

#include "nix/store/spotlight-apps.hh"

#include "nix/util/logging.hh"
#include "nix/util/users.hh"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

extern char ** environ;

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
    std::ifstream f(infoPlist, std::ios::binary);
    if (!f)
        return {};

    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
    std::error_code ec;
    if (fs::exists(direct, ec))
        return direct;

    if (!fs::path(iconKey).has_extension()) {
        for (const char * ext : {".icns", ".png", ".tiff", ".jpg"}) {
            fs::path withExt = resourcesDir / (iconKey + ext);
            if (fs::exists(withExt, ec))
                return withExt;
        }
    }

    return std::nullopt;
}

/**
 * Run an external command, swallowing its output. Used for `mdimport -i`.
 * Returns the child's exit status, or -1 on spawn failure.
 */
static int spawnQuiet(const std::vector<std::string> & argv)
{
    if (argv.empty())
        return -1;

    std::vector<char *> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto & a : argv)
        cargv.push_back(const_cast<char *>(a.c_str()));
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    /* Detach stdout/stderr so we don't pollute the user's terminal. */
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = -1;
    int rc = posix_spawn(&pid, cargv[0], &actions, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0)
        return -1;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/**
 * (Re)create `dir` as an empty directory. If wiping fails, warn and
 * continue - stale entries may persist but the new generation's bundles
 * will still be materialized on top.
 */
static void resetDir(const fs::path & dir)
{
    std::error_code removeEc;
    fs::remove_all(dir, removeEc);
    if (removeEc && fs::exists(dir))
        warn(
            "could not fully clear Spotlight app shadow directory %s: %s "
            "(stale entries from a previous profile generation may remain)",
            dir.string(),
            removeEc.message());

    std::error_code createEc;
    fs::create_directories(dir, createEc);
}

/**
 * Symlink `link` -> `target`, replacing any existing entry at `link`.
 */
static void replaceSymlink(const fs::path & target, const fs::path & link)
{
    std::error_code ec;
    fs::remove(link, ec);
    fs::create_symlink(target, link);
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

    {
        std::error_code ec;
        if (!fs::is_directory(srcContents, ec)) {
            /* Not actually a bundle (e.g. a stray file with `.app` suffix). */
            return false;
        }
    }
    {
        std::error_code ec;
        if (!fs::is_regular_file(srcInfoPlist, ec)) {
            /* No Info.plist -> can't be made into something Spotlight will
               index as an Application. Skip silently. */
            return false;
        }
    }

    /* Read CFBundleIconFile so we know what icon to copy. */
    auto plist = readInfoPlist(srcInfoPlist);
    std::optional<std::string> iconKey;
    if (plist) {
        auto k = cfStr("CFBundleIconFile");
        iconKey = dictString(plist.get(), k.get());
    }

    fs::path destContents = destApp / "Contents";
    fs::path destResources = destContents / "Resources";
    fs::create_directories(destResources);

    /* Copy Info.plist (mandatory). */
    fs::copy_file(srcInfoPlist, destContents / "Info.plist", fs::copy_options::overwrite_existing);

    /* Copy PkgInfo if present (optional, harmless if missing). */
    {
        fs::path pkgInfo = srcContents / "PkgInfo";
        std::error_code ec;
        if (fs::is_regular_file(pkgInfo, ec))
            fs::copy_file(pkgInfo, destContents / "PkgInfo", fs::copy_options::overwrite_existing);
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
            std::error_code ec;
            fs::copy_file(*iconPath, destIcon, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                copiedIconFile = iconPath->filename();
        }
    }

    /* Symlink everything else under Contents/ into the store. */
    {
        std::error_code ec;
        for (auto & entry : fs::directory_iterator(srcContents, ec)) {
            const auto & name = entry.path().filename();
            if (name == "Info.plist" || name == "PkgInfo" || name == "Resources")
                continue;
            replaceSymlink(entry.path(), destContents / name);
        }
        if (ec)
            return false;
    }

    /* Symlink everything else under Resources/ into the store, except the
       icon file we copied above. */
    {
        std::error_code ec;
        if (fs::is_directory(srcResources, ec)) {
            std::error_code iterEc;
            for (auto & entry : fs::directory_iterator(srcResources, iterEc)) {
                const auto & name = entry.path().filename();
                if (copiedIconFile && name == *copiedIconFile)
                    continue;
                replaceSymlink(entry.path(), destResources / name);
            }
            if (iterEc)
                warn(
                    "could not fully enumerate %s while materializing %s: %s "
                    "(some resource symlinks may be missing - Spotlight will "
                    "still index the bundle but launching may be impaired)",
                    srcResources.string(),
                    destApp.string(),
                    iterEc.message());
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

    std::error_code ec;
    if (!fs::is_directory(appsDir, ec))
        return out;

    for (auto & entry : fs::directory_iterator(appsDir, ec)) {
        if (entry.path().extension() != ".app")
            continue;
        std::error_code rc;
        /* Resolve through the buildenv symlink chain so that destApp's
           symlinks point at the canonical store path, not at another
           symlink farm. This keeps the materialized bundle valid even if
           the intermediate generation directories are GC'd later (the
           canonical store path is what the profile pins as a GC root). */
        fs::path resolved = fs::canonical(entry.path(), rc);
        if (rc)
            continue;
        out.push_back(std::move(resolved));
    }

    return out;
}

} // anonymous namespace

namespace detail {

void syncProfileAppBundlesAt(const fs::path & profile, const fs::path & dest, bool notifySystem) noexcept
{
    try {
        std::error_code ec;

        /* Resolve the profile symlink (e.g. `~/.nix-profile`) to the actual
           generation directory it currently points at. If the profile does
           not exist or does not resolve, treat that as "no apps" and let
           the cleanup branch below remove our destination. */
        fs::path target;
        if (fs::exists(profile, ec))
            target = fs::canonical(profile, ec);

        std::vector<fs::path> bundles;
        if (!target.empty())
            bundles = findAppBundles(target);

        /* If there are no apps in the new profile, just remove our
           destination directory and we're done. This is the only path by
           which an *uninstall* propagates to Spotlight: the next profile
           switch sees fewer apps and rebuilds the destination accordingly. */
        if (bundles.empty()) {
            if (fs::exists(dest, ec))
                fs::remove_all(dest, ec);
            return;
        }

        /* Wipe + rebuild. We own this directory outright; nothing else on
           the system writes to "Nix Profile Apps". This is the simplest
           possible state model and avoids any need to track which bundles
           we created on a previous run. */
        resetDir(dest);

        size_t ok = 0;
        for (const auto & srcApp : bundles) {
            fs::path destApp = dest / srcApp.filename();
            try {
                if (materializeHybridBundle(srcApp, destApp, notifySystem))
                    ok++;
            } catch (const std::exception & e) {
                warn("could not materialize hybrid app bundle for %s: %s", srcApp.string(), e.what());
            }
        }

        /* Nudge Spotlight to index the new bundles now instead of waiting
           for its next periodic walk. Best-effort; ignore the exit status. */
        if (notifySystem && ok > 0)
            (void) spawnQuiet({"/usr/bin/mdimport", "-i", dest.string()});

        debug("syncProfileAppBundles: materialized %d/%d bundles into %s", ok, bundles.size(), dest.string());
    } catch (const std::exception & e) {
        warn("syncProfileAppBundles failed: %s", e.what());
    } catch (...) {
        warn("syncProfileAppBundles failed with an unknown exception");
    }
}

} // namespace detail

void syncProfileAppBundles(const fs::path & profile) noexcept
{
    detail::syncProfileAppBundlesAt(profile, destinationDir(), /* notifySystem = */ true);
}

} // namespace nix::darwin
