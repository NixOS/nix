#include "shared.hh"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <algorithm>

using namespace nix;

typedef std::map<Path,int> Priorities;

static bool isDirectory (const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == -1)
        throw SysError(format("getting status of '%1%'") % path);
    return S_ISDIR(st.st_mode);
}

static auto priorities = Priorities{};

static auto symlinks = 0;

/* For each activated package, create symlinks */
static void createLinks(const Path & srcDir, const Path & dstDir, int priority)
{
    auto srcFiles = readDirectory(srcDir);
    for (const auto & ent : srcFiles) {
        if (ent.name[0] == '.')
            /* not matched by glob */
            continue;
        const auto & srcFile = srcDir + "/" + ent.name;
        auto dstFile = dstDir + "/" + ent.name;

        /* The files below are special-cased to that they don't show up
         * in user profiles, either because they are useless, or
         * because they would cauase pointless collisions (e.g., each
         * Python package brings its own
         * `$out/lib/pythonX.Y/site-packages/easy-install.pth'.)
         */
        if (hasSuffix(srcFile, "/propagated-build-inputs") ||
            hasSuffix(srcFile, "/nix-support") ||
            hasSuffix(srcFile, "/perllocal.pod") ||
            hasSuffix(srcFile, "/info/dir") ||
            hasSuffix(srcFile, "/log")) {
            continue;
        } else if (isDirectory(srcFile)) {
            struct stat dstSt;
            auto res = lstat(dstFile.c_str(), &dstSt);
            if (res == 0) {
                if (S_ISDIR(dstSt.st_mode)) {
                    createLinks(srcFile, dstFile, priority);
                    continue;
                } else if (S_ISLNK(dstSt.st_mode)) {
                    auto target = readLink(dstFile);
                    if (!isDirectory(target))
                        throw Error(format("collision between '%1%' and non-directory '%2%'")
                            % srcFile % target);
                    if (unlink(dstFile.c_str()) == -1)
                        throw SysError(format("unlinking '%1%'") % dstFile);
                    if (mkdir(dstFile.c_str(), 0755) == -1)
                        throw SysError(format("creating directory '%1%'"));
                    createLinks(target, dstFile, priorities[dstFile]);
                    createLinks(srcFile, dstFile, priority);
                    continue;
                }
            } else if (errno != ENOENT)
                throw SysError(format("getting status of '%1%'") % dstFile);
        } else {
            struct stat dstSt;
            auto res = lstat(dstFile.c_str(), &dstSt);
            if (res == 0) {
                if (S_ISLNK(dstSt.st_mode)) {
                    auto target = readLink(dstFile);
                    auto prevPriority = priorities[dstFile];
                    if (prevPriority == priority)
                        throw Error(format(
                                "packages '%1%' and '%2%' have the same priority %3%; "
                                "use 'nix-env --set-flag priority NUMBER INSTALLED_PKGNAME' "
                                "to change the priority of one of the conflicting packages"
                                " (0 being the highest priority)"
                                ) % srcFile % target % priority);
                    if (prevPriority < priority)
                        continue;
                    if (unlink(dstFile.c_str()) == -1)
                        throw SysError(format("unlinking '%1%'") % dstFile);
                }
            } else if (errno != ENOENT)
                throw SysError(format("getting status of '%1%'") % dstFile);
        }
        createSymlink(srcFile, dstFile);
        priorities[dstFile] = priority;
        symlinks++;
    }
}

typedef std::set<Path> FileProp;

static auto done = FileProp{};
static auto postponed = FileProp{};

static auto out = string{};

static void addPkg(const Path & pkgDir, int priority)
{
    if (done.find(pkgDir) != done.end())
        return;
    done.insert(pkgDir);
    createLinks(pkgDir, out, priority);
    auto propagatedFN = pkgDir + "/nix-support/propagated-user-env-packages";
    auto propagated = string{};
    {
        AutoCloseFD fd = open(propagatedFN.c_str(), O_RDONLY | O_CLOEXEC);
        if (!fd) {
            if (errno == ENOENT)
                return;
            throw SysError(format("opening '%1%'") % propagatedFN);
        }
        propagated = readFile(fd.get());
    }
    for (const auto & p : tokenizeString<std::vector<string>>(propagated, " \n"))
        if (done.find(p) == done.end())
            postponed.insert(p);
}

struct Package {
    Path path;
    bool active;
    int priority;
    Package(Path path, bool active, int priority) : path{std::move(path)}, active{active}, priority{priority} {}
};

typedef std::vector<Package> Packages;

int main(int argc, char ** argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();
        out = getEnv("out");
        if (mkdir(out.c_str(), 0755) == -1)
            throw SysError(format("creating %1%") % out);

        /* Convert the stuff we get from the environment back into a coherent
         * data type.
         */
        auto pkgs = Packages{};
        auto derivations = tokenizeString<Strings>(getEnv("derivations"));
        while (!derivations.empty()) {
            /* !!! We're trusting the caller to structure derivations env var correctly */
            auto active = derivations.front(); derivations.pop_front();
            auto priority = stoi(derivations.front()); derivations.pop_front();
            auto outputs = stoi(derivations.front()); derivations.pop_front();
            for (auto n = 0; n < outputs; n++) {
                auto path = derivations.front(); derivations.pop_front();
                pkgs.emplace_back(path, active != "false", priority);
            }
        }

        /* Symlink to the packages that have been installed explicitly by the
         * user. Process in priority order to reduce unnecessary
         * symlink/unlink steps.
         */
        std::sort(pkgs.begin(), pkgs.end(), [](const Package & a, const Package & b) {
            return a.priority < b.priority || (a.priority == b.priority && a.path < b.path);
        });
        for (const auto & pkg : pkgs)
            if (pkg.active)
                addPkg(pkg.path, pkg.priority);

        /* Symlink to the packages that have been "propagated" by packages
         * installed by the user (i.e., package X declares that it wants Y
         * installed as well). We do these later because they have a lower
         * priority in case of collisions.
         */
        auto priorityCounter = 1000;
        while (!postponed.empty()) {
            auto pkgDirs = postponed;
            postponed = FileProp{};
            for (const auto & pkgDir : pkgDirs)
                addPkg(pkgDir, priorityCounter++);
        }

        std::cerr << "created " << symlinks << " symlinks in user environment\n";

        createSymlink(getEnv("manifest"), out + "/manifest.nix");
    });
}

