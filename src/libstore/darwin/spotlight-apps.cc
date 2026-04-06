#include "nix/store/spotlight-apps.hh"

#include "spotlight-apps-private.hh"

#include "nix/util/deleter.hh"
#include "nix/util/error.hh"
#include "nix/util/file-system.hh"
#include "nix/util/hash.hh"
#include "nix/util/logging.hh"
#include "nix/util/processes.hh"
#include "nix/util/users.hh"
#include "nix/util/util.hh"

#include <sys/stat.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

namespace nix::darwin {

namespace {

namespace fs = std::filesystem;

template<typename T>
using CFRef = std::unique_ptr<std::remove_pointer_t<T>, Deleter<CFRelease>>;

static CFRef<CFURLRef> cfURL(const fs::path & path, bool isDirectory)
{
    auto s = path.string();
    return CFRef<CFURLRef>(CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(s.data()), s.size(), isDirectory));
}

static std::optional<std::string> cfString(CFStringRef string)
{
    if (const char * ptr = CFStringGetCStringPtr(string, kCFStringEncodingUTF8))
        return ptr;

    CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer(size);
    if (!CFStringGetCString(string, buffer.data(), size, kCFStringEncodingUTF8))
        return std::nullopt;

    return buffer.data();
}

static bool isDirectory(const fs::path & path)
{
    auto st = maybeStat(path);
    return st && S_ISDIR(st->st_mode);
}

static bool isRegularFile(const fs::path & path)
{
    auto st = maybeStat(path);
    return st && S_ISREG(st->st_mode);
}

static std::optional<fs::path> iconFileName(CFBundleRef bundle)
{
    auto value = CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFBundleIconFile"));
    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return std::nullopt;

    auto icon = cfString(static_cast<CFStringRef>(value));
    if (!icon)
        return std::nullopt;

    fs::path iconPath = *icon;
    if (iconPath.empty() || iconPath.is_absolute() || iconPath.has_parent_path())
        return std::nullopt;

    if (iconPath.extension().empty())
        iconPath += ".icns";

    return iconPath;
}

static std::optional<fs::path>
materializeIcon(CFBundleRef bundle, const fs::path & srcResources, const fs::path & destResources)
{
    auto icon = iconFileName(bundle);
    if (!icon)
        return std::nullopt;

    fs::path srcIcon = srcResources / *icon;
    if (!isRegularFile(srcIcon))
        return std::nullopt;

    copyFile(srcIcon, destResources / *icon, /*andDelete=*/false, /*contents=*/true);
    return icon;
}

static void materializeResources(CFBundleRef bundle, const fs::path & srcResources, const fs::path & destResources)
{
    if (!isDirectory(srcResources))
        return;

    createDirs(destResources);

    auto copiedIcon = materializeIcon(bundle, srcResources, destResources);

    for (auto & entry : DirectoryIterator{srcResources}) {
        auto name = entry.path().filename();
        if (copiedIcon && name == *copiedIcon)
            continue;
        createSymlink(entry.path(), destResources / name);
    }
}

static void copyBundleMetadata(const fs::path & srcContents, const fs::path & destContents)
{
    copyFile(srcContents / "Info.plist", destContents / "Info.plist", /*andDelete=*/false, /*contents=*/true);

    fs::path srcPkgInfo = srcContents / "PkgInfo";
    if (isRegularFile(srcPkgInfo))
        copyFile(srcPkgInfo, destContents / "PkgInfo", /*andDelete=*/false, /*contents=*/true);
}

static void symlinkRuntimeContents(const fs::path & srcContents, const fs::path & destContents)
{
    for (auto & entry : DirectoryIterator{srcContents}) {
        auto name = entry.path().filename();
        if (name == "Info.plist" || name == "PkgInfo" || name == "Resources")
            continue;
        createSymlink(entry.path(), destContents / name);
    }
}

static void registerAppBundle(const fs::path & destApp)
{
    auto url = cfURL(destApp, true);
    if (!url)
        return;

    auto status = LSRegisterURL(url.get(), true);
    if (status != noErr)
        warn("could not register app bundle %s with Launch Services: OSStatus %d", PathFmt(destApp), status);
}

static bool materializeAppBundle(const fs::path & srcApp, const fs::path & destApp, bool notifySystem)
{
    fs::path srcContents = srcApp / "Contents";
    fs::path srcInfoPlist = srcContents / "Info.plist";

    if (!isDirectory(srcContents))
        return false;

    if (!isRegularFile(srcInfoPlist))
        return false;

    auto bundleURL = cfURL(srcApp, true);
    auto bundle = CFRef<CFBundleRef>(bundleURL ? CFBundleCreate(kCFAllocatorDefault, bundleURL.get()) : nullptr);
    if (!bundle)
        return false;

    fs::path destContents = destApp / "Contents";
    createDirs(destContents);

    copyBundleMetadata(srcContents, destContents);
    materializeResources(bundle.get(), srcContents / "Resources", destContents / "Resources");
    symlinkRuntimeContents(srcContents, destContents);

    if (notifySystem)
        registerAppBundle(destApp);

    return true;
}

static std::string profileDirName(const fs::path & profile)
{
    return "profile-"
           + hashString(HashAlgorithm::SHA256, absPath(profile).string()).to_string(HashFormat::Nix32, false);
}

static fs::path destinationBaseDir()
{
    if (isRootUser())
        return "/Applications/Nix Profile Apps";

    return getHome() / "Applications" / "Nix Profile Apps";
}

static std::vector<fs::path> findAppBundles(const fs::path & profileTarget)
{
    std::vector<fs::path> out;
    fs::path appsDir = profileTarget / "Applications";

    try {
        if (!isDirectory(appsDir))
            return out;

        for (auto & entry : DirectoryIterator{appsDir}) {
            if (entry.path().extension() != ".app")
                continue;
            try {
                out.push_back(canonPath(entry.path(), /*resolveSymlinks=*/true));
            } catch (Error & e) {
                warn("could not resolve app bundle %s for Spotlight indexing: %s", PathFmt(entry.path()), e.what());
            }
        }
    } catch (Error & e) {
        warn("could not enumerate %s for Spotlight indexing: %s", PathFmt(appsDir), e.what());
    }

    return out;
}

static std::optional<fs::path> resolveProfileTarget(const fs::path & profile)
{
    try {
        if (!pathExists(profile))
            return std::nullopt;

        return canonPath(profile, /*resolveSymlinks=*/true);
    } catch (Error & e) {
        warn("could not resolve profile %s for Spotlight indexing: %s", PathFmt(profile), e.what());
        return std::nullopt;
    }
}

static bool removeProfileAppBundlesDir(const fs::path & dest)
{
    try {
        deletePath(dest);
        return true;
    } catch (Error & e) {
        warn("could not remove stale Spotlight app bundle directory %s: %s", PathFmt(dest), e.what());
        return false;
    }
}

static bool recreateProfileAppBundlesDir(const fs::path & dest)
{
    if (!removeProfileAppBundlesDir(dest))
        return false;

    try {
        createDirs(dest);
        return true;
    } catch (Error & e) {
        warn("could not create Spotlight app bundle directory %s: %s", PathFmt(dest), e.what());
        return false;
    }
}

static void cleanupFailedBundle(const fs::path & destApp)
{
    try {
        deletePath(destApp);
    } catch (Error & e) {
        debug("could not clean up failed Spotlight app bundle %s: %s", PathFmt(destApp), e.what());
    }
}

static void importProfileAppBundles(const fs::path & dest)
{
    try {
        (void) runProgram(
            RunOptions{
                .program = "/usr/bin/mdimport",
                .lookupPath = false,
                .args = {"-i", dest.string()},
                .mergeStderrToStdout = true,
            });
    } catch (Error & e) {
        warn("could not ask Spotlight to import app bundles in %s: %s", PathFmt(dest), e.what());
    }
}

} // anonymous namespace

namespace detail {

fs::path profileAppBundlesDirAt(const fs::path & profile, const fs::path & base)
{
    return base / profileDirName(profile);
}

void syncProfileAppBundlesAt(const fs::path & profile, const fs::path & dest, bool notifySystem)
{
    auto target = resolveProfileTarget(profile);
    std::vector<fs::path> bundles;
    if (target)
        bundles = findAppBundles(*target);

    if (bundles.empty()) {
        removeProfileAppBundlesDir(dest);
        return;
    }

    if (!recreateProfileAppBundlesDir(dest))
        return;

    size_t ok = 0;
    for (const auto & srcApp : bundles) {
        fs::path destApp = dest / srcApp.filename();

        try {
            if (materializeAppBundle(srcApp, destApp, notifySystem))
                ok++;
            else
                cleanupFailedBundle(destApp);
        } catch (Error & e) {
            warn("could not materialize app bundle for %s: %s", PathFmt(srcApp), e.what());
            cleanupFailedBundle(destApp);
        }
    }

    if (notifySystem && ok > 0)
        importProfileAppBundles(dest);

    debug("syncProfileAppBundles: materialized %d/%d bundles into %s", ok, bundles.size(), PathFmt(dest));
}

} // namespace detail

void syncProfileAppBundles(const fs::path & profile) noexcept
{
    try {
        detail::syncProfileAppBundlesAt(
            profile, detail::profileAppBundlesDirAt(profile, destinationBaseDir()), /* notifySystem = */ true);
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

} // namespace nix::darwin
