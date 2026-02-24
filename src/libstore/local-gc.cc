#include "nix/store/gc-store.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/types.hh"
#include "nix/store/local-gc.hh"
#include <filesystem>
#include <boost/regex.hpp>

#if !defined(__linux__)
// For shelling out to lsof
#  include "store-config-private.hh"
#  include "nix/util/environment-variables.hh"
#  include "nix/util/processes.hh"
#endif

namespace nix {

/**
 * Key is a mere string because cannot has path with macOS's libc++
 */
typedef boost::unordered_flat_map<
    std::string,
    boost::unordered_flat_set<std::string, StringViewHash, std::equal_to<>>,
    StringViewHash,
    std::equal_to<>>
    UncheckedRoots;

static void readProcLink(const std::filesystem::path & file, UncheckedRoots & roots)
{
    std::filesystem::path buf;
    try {
        buf = std::filesystem::read_symlink(file);
    } catch (std::filesystem::filesystem_error & e) {
        if (e.code() == std::errc::no_such_file_or_directory || e.code() == std::errc::permission_denied
            || e.code() == std::errc::no_such_process)
            return;
        throw SystemError(e.code(), "reading symlink '%s'", PathFmt(file));
    }
    if (buf.is_absolute())
        roots[buf.string()].emplace(file.string());
}

static std::string quoteRegexChars(const std::string & raw)
{
    static auto specialRegex = boost::regex(R"([.^$\\*+?()\[\]{}|])");
    return boost::regex_replace(raw, specialRegex, R"(\\$&)");
}

#ifdef __linux__
static void readFileRoots(const std::filesystem::path & path, UncheckedRoots & roots)
{
    try {
        roots[readFile(path)].emplace(path.string());
    } catch (SystemError & e) {
        if (!e.is(std::errc::no_such_file_or_directory) && !e.is(std::errc::permission_denied))
            throw;
    }
}
#endif

Roots findRuntimeRootsUnchecked(const StoreDirConfig & config)
{
    UncheckedRoots unchecked;

    auto procDir = AutoCloseDir{opendir("/proc")};
    if (procDir) {
        struct dirent * ent;
        static const auto digitsRegex = boost::regex(R"(^\d+$)");
        static const auto mapRegex = boost::regex(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
        auto storePathRegex = boost::regex(quoteRegexChars(config.storeDir) + R"(/[0-9a-z]+[0-9a-zA-Z\+\-\._\?=]*)");
        while (errno = 0, ent = readdir(procDir.get())) {
            checkInterrupt();
            if (boost::regex_match(ent->d_name, digitsRegex)) {
                try {
                    readProcLink(fmt("/proc/%s/exe", ent->d_name), unchecked);
                    readProcLink(fmt("/proc/%s/cwd", ent->d_name), unchecked);

                    auto fdStr = fmt("/proc/%s/fd", ent->d_name);
                    auto fdDir = AutoCloseDir(opendir(fdStr.c_str()));
                    if (!fdDir) {
                        if (errno == ENOENT || errno == EACCES)
                            continue;
                        throw SysError("opening %1%", fdStr);
                    }
                    struct dirent * fd_ent;
                    while (errno = 0, fd_ent = readdir(fdDir.get())) {
                        if (fd_ent->d_name[0] != '.')
                            readProcLink(fmt("%s/%s", fdStr, fd_ent->d_name), unchecked);
                    }
                    if (errno) {
                        if (errno == ESRCH)
                            continue;
                        throw SysError("iterating /proc/%1%/fd", ent->d_name);
                    }
                    fdDir.reset();

                    std::filesystem::path mapFile = fmt("/proc/%s/maps", ent->d_name);
                    auto mapLines = tokenizeString<std::vector<std::string>>(readFile(mapFile.string()), "\n");
                    for (const auto & line : mapLines) {
                        auto match = boost::smatch{};
                        if (boost::regex_match(line, match, mapRegex))
                            unchecked[match[1]].emplace(mapFile.string());
                    }

                    auto envFile = fmt("/proc/%s/environ", ent->d_name);
                    auto envString = readFile(envFile);
                    auto env_end = boost::sregex_iterator{};
                    for (auto i = boost::sregex_iterator{envString.begin(), envString.end(), storePathRegex};
                         i != env_end;
                         ++i)
                        unchecked[i->str()].emplace(envFile);
                } catch (SystemError & e) {
                    if (errno == ENOENT || errno == EACCES || errno == ESRCH)
                        continue;
                    throw;
                }
            }
        }
        if (errno)
            throw SysError("iterating /proc");
    }

#if !defined(__linux__)
    // lsof is really slow on OS X. This actually causes the gc-concurrent.sh test to fail.
    // See: https://github.com/NixOS/nix/issues/3011
    // Because of this we disable lsof when running the tests.
    if (getEnv("_NIX_TEST_NO_LSOF") != "1") {
        try {
            boost::regex lsofRegex(R"(^n(/.*)$)");
            auto lsofLines = tokenizeString<std::vector<std::string>>(
                runProgram(LSOF, true, {OS_STR("-n"), OS_STR("-w"), OS_STR("-F"), OS_STR("n")}), "\n");
            for (const auto & line : lsofLines) {
                boost::smatch match;
                if (boost::regex_match(line, match, lsofRegex))
                    unchecked[match[1].str()].emplace("{lsof}");
            }
        } catch (ExecError & e) {
            /* lsof not installed, lsof failed */
        }
    }
#endif

#ifdef __linux__
    readFileRoots("/proc/sys/kernel/modprobe", unchecked);
    readFileRoots("/proc/sys/kernel/fbsplash", unchecked);
    readFileRoots("/proc/sys/kernel/poweroff_cmd", unchecked);
#endif

    Roots roots;

    for (auto & [target, links] : unchecked) {
        if (!config.isInStore(target))
            continue;
        try {
            auto path = config.toStorePath(target).first;
            roots[path].insert(links.begin(), links.end());
        } catch (BadStorePath &) {
        }
    }

    return roots;
}

} // namespace nix
