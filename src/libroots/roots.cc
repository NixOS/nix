/**
 * @file
 *
 * A very simple utility to trace all the gc roots through the file-system
 * The reason for this program is that tracing these roots is the only part of
 * Nix that requires to run as root (because it requires reading through the
 * user home directories to resolve the indirect roots)
 */

#include "nix/roots/roots.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/signals.hh"
#include "nix/util/strings.hh"

#include "roots-config-private.hh"

#include <boost/regex.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <unistd.h>
#include <vector>
#include <algorithm>

namespace nix::roots_tracer {
static std::string censored = "{censored}";

static bool isInStore(std::filesystem::path storeDir, std::filesystem::path dir)
{
    return (std::search(dir.begin(), dir.end(), storeDir.begin(), storeDir.end()) == dir.begin());
}

static void readProcLink(const std::filesystem::path & file, UncheckedRoots & roots)
{
    std::filesystem::path buf;
    try {
        buf = std::filesystem::read_symlink(file);
    } catch (std::filesystem::filesystem_error & e) {
        if (e.code() == std::errc::no_such_file_or_directory || e.code() == std::errc::permission_denied
            || e.code() == std::errc::no_such_process)
            return;
        throw;
    }
    if (buf.is_absolute())
        roots[buf.string()].emplace(file.string());
}

static std::string quoteRegexChars(const std::string & raw)
{
    static auto specialRegex = boost::regex(R"([.^$\\*+?()\[\]{}|])");
    return boost::regex_replace(raw, specialRegex, R"(\$&)");
}

static boost::regex makeStorePathRegex(const std::filesystem::path storeDir)
{
    return boost::regex(quoteRegexChars(std::string(storeDir) + "/") + R"((?!\.\.?(-|$))[0-9a-zA-Z\+\-\._\?=]+)");
}

static bool isStorePath(const boost::regex & storePathRegex, const std::string & path)
{
    // On Windows, `/nix/store` is not a canonical path. More broadly it
    // is unclear whether this function should be using the native
    // notion of a canonical path at all. For example, it makes to
    // support remote stores whose store dir is a non-native path (e.g.
    // Windows <-> Unix ssh-ing).
    auto p =
#ifdef _WIN32
        path
#else
        canonPath(path)
#endif
        ;

    return boost::regex_match(p, storePathRegex);
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

void findRuntimeRoots(const TracerConfig & opts, UncheckedRoots & roots, bool censor)
{
    UncheckedRoots unchecked;

#ifdef __linux__
    // The /proc directory either doesn't exist or looks very different on other OSes,
    // so only bother attempting on linux.
    auto procDir = AutoCloseDir{opendir("/proc")};
    if (procDir) {
        struct dirent * ent;
        static const auto digitsRegex = boost::regex(R"(^\d+$)");
        static const auto mapRegex = boost::regex(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
        auto storePathRegex = makeStorePathRegex(opts.storeDir);
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
    readFileRoots("/proc/sys/kernel/modprobe", unchecked);
    readFileRoots("/proc/sys/kernel/fbsplash", unchecked);
    readFileRoots("/proc/sys/kernel/poweroff_cmd", unchecked);
#else
    // lsof is really slow on OS X. This actually causes the gc-concurrent.sh test to fail.
    // See: https://github.com/NixOS/nix/issues/3011
    // Because of this we disable lsof when running the tests.
    if (getEnv("_NIX_TEST_NO_LSOF") != "1") {
        try {
            boost::regex lsofRegex(R"(^n(/.*)$)");
            auto lsofLines =
                tokenizeString<std::vector<std::string>>(runProgram(LSOF, true, {"-n", "-w", "-F", "n"}), "\n");
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

    for (auto & [target, links] : unchecked) {
        if (!isInStore(opts.storeDir, target))
            continue;
        if (censor)
            roots[target].insert(censored);
        else
            roots[target].insert(links.begin(), links.end());
    }
}

void findRoots(const TracerConfig & opts, const Path & path, std::filesystem::file_type type, UncheckedRoots & roots)
{
    auto storePathRegex = makeStorePathRegex(opts.storeDir);

    auto foundRoot = [&](const Path & path, const Path & target) {
        boost::smatch match;
        if (boost::regex_match(target, match, storePathRegex))
            roots[target].emplace(path);
    };

    try {

        if (type == std::filesystem::file_type::unknown)
            type = std::filesystem::symlink_status(path).type();

        if (type == std::filesystem::file_type::directory) {
            for (auto & i : DirectoryIterator{path}) {
                checkInterrupt();
                findRoots(opts, i.path().string(), i.symlink_status().type(), roots);
            }
        }

        else if (type == std::filesystem::file_type::symlink) {
            Path target = readLink(path);
            if (isInStore(opts.storeDir, target))
                foundRoot(path, target);

            /* Handle indirect roots. */
            else {
                target = absPath(target, dirOf(path));
                if (!pathExists(target)) {
                    if (isInDir(path, opts.stateDir / "gcroots" / "auto")) {
                        printInfo("removing stale link from '%1%' to '%2%'", path, target);
                        unlink(path.c_str());
                    }
                } else {
                    if (!std::filesystem::is_symlink(target))
                        return;
                    Path target2 = readLink(target);
                    if (isInStore(opts.storeDir, target2))
                        foundRoot(target, target2);
                }
            }
        }

        else if (type == std::filesystem::file_type::regular) {
            auto storePath = std::string(opts.storeDir / std::string(baseNameOf(path)));
            if (isStorePath(storePathRegex, storePath))
                roots[storePath].emplace(path);
        }

    }

    catch (std::filesystem::filesystem_error & e) {
        /* We only ignore permanent failures. */
        if (e.code() == std::errc::permission_denied || e.code() == std::errc::no_such_file_or_directory
            || e.code() == std::errc::not_a_directory)
            printInfo("cannot read potential root '%1%'", path);
        else
            throw;
    }

    catch (SystemError & e) {
        /* We only ignore permanent failures. */
        if (e.is(std::errc::permission_denied) || e.is(std::errc::no_such_file_or_directory)
            || e.is(std::errc::not_a_directory))
            printInfo("cannot read potential root '%1%'", path);
        else
            throw;
    }
}

} // namespace nix::roots_tracer
