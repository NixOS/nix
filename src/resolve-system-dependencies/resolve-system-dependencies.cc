#include "derivations.hh"
#include "globals.hh"
#include "shared.hh"
#include "store-api.hh"
#include <sys/utsname.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sys/mman.h>
#include <fcntl.h>
#include <mach-o/loader.h>
#include <mach-o/swap.h>

#define DO_SWAP(x, y) ((x) ? OSSwapInt32(y) : (y))

using namespace nix;

static auto cacheDir = Path{};

Path resolveCacheFile(Path lib)
{
    std::replace(lib.begin(), lib.end(), '/', '%');
    return cacheDir + "/" + lib;
}

std::set<string> readCacheFile(const Path & file)
{
    return tokenizeString<set<string>>(readFile(file), "\n");
}

std::set<std::string> runResolver(const Path & filename)
{
    AutoCloseFD fd = open(filename.c_str(), O_RDONLY);
    if (!fd)
        throw SysError("opening '%s'", filename);

    struct stat st;
    if (fstat(fd.get(), &st))
        throw SysError("statting '%s'", filename);

    if (!S_ISREG(st.st_mode)) {
        printError("file '%s' is not a regular file", filename);
        return {};
    }

    if (st.st_size < sizeof(mach_header_64)) {
        printError("file '%s' is too short for a MACH binary", filename);
        return {};
    }

    char* obj = (char*) mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd.get(), 0);
    if (!obj)
        throw SysError("mmapping '%s'", filename);

    ptrdiff_t mach64_offset = 0;

    uint32_t magic = ((mach_header_64*) obj)->magic;
    if (magic == FAT_CIGAM || magic == FAT_MAGIC) {
        bool should_swap = magic == FAT_CIGAM;
        uint32_t narches = DO_SWAP(should_swap, ((fat_header *) obj)->nfat_arch);
        for (uint32_t i = 0; i < narches; i++) {
            fat_arch* arch = (fat_arch*) (obj + sizeof(fat_header) + sizeof(fat_arch) * i);
            if (DO_SWAP(should_swap, arch->cputype) == CPU_TYPE_X86_64) {
                mach64_offset = (ptrdiff_t) DO_SWAP(should_swap, arch->offset);
                break;
            }
        }
        if (mach64_offset == 0) {
            printError(format("Could not find any mach64 blobs in file '%1%', continuing...") % filename);
            return {};
        }
    } else if (magic == MH_MAGIC_64 || magic == MH_CIGAM_64) {
        mach64_offset = 0;
    } else {
        printError(format("Object file has unknown magic number '%1%', skipping it...") % magic);
        return {};
    }

    mach_header_64 * m_header = (mach_header_64 *) (obj + mach64_offset);

    bool should_swap = magic == MH_CIGAM_64;
    ptrdiff_t cmd_offset = mach64_offset + sizeof(mach_header_64);

    std::set<string> libs;
    for (uint32_t i = 0; i < DO_SWAP(should_swap, m_header->ncmds); i++) {
        load_command * cmd = (load_command *) (obj + cmd_offset);
        switch(DO_SWAP(should_swap, cmd->cmd)) {
            case LC_LOAD_UPWARD_DYLIB:
            case LC_LOAD_DYLIB:
            case LC_REEXPORT_DYLIB:
                libs.insert(std::string((char *) cmd + ((dylib_command*) cmd)->dylib.name.offset));
                break;
        }
        cmd_offset += DO_SWAP(should_swap, cmd->cmdsize);
    }

    return libs;
}

bool isSymlink(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st) == -1)
        throw SysError("getting attributes of path '%1%'", path);

    return S_ISLNK(st.st_mode);
}

Path resolveSymlink(const Path & path)
{
    auto target = readLink(path);
    return hasPrefix(target, "/")
        ? target
        : dirOf(path) + "/" + target;
}

std::set<string> resolveTree(const Path & path, PathSet & deps)
{
    std::set<string> results;
    if (deps.count(path))
        return {};
    deps.insert(path);
    for (auto & lib : runResolver(path)) {
        results.insert(lib);
        for (auto & p : resolveTree(lib, deps)) {
            results.insert(p);
        }
    }
    return results;
}

std::set<string> getPath(const Path & path)
{
    if (hasPrefix(path, "/dev")) return {};

    Path cacheFile = resolveCacheFile(path);
    if (pathExists(cacheFile))
        return readCacheFile(cacheFile);

    std::set<string> deps, paths;
    paths.insert(path);

    Path nextPath(path);
    while (isSymlink(nextPath)) {
        nextPath = resolveSymlink(nextPath);
        paths.insert(nextPath);
    }

    for (auto & t : resolveTree(nextPath, deps))
        paths.insert(t);

    writeFile(cacheFile, concatStringsSep("\n", paths));

    return paths;
}

int main(int argc, char ** argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();

        struct utsname _uname;

        uname(&_uname);

        auto cacheParentDir = (format("%1%/dependency-maps") % settings.nixStateDir).str();

        cacheDir = (format("%1%/%2%-%3%-%4%")
                % cacheParentDir
                % _uname.machine
                % _uname.sysname
                % _uname.release).str();

        mkdir(cacheParentDir.c_str(), 0755);
        mkdir(cacheDir.c_str(), 0755);

        auto store = openStore();

        StringSet impurePaths;

        if (std::string(argv[1]) == "--test")
            impurePaths.insert(argv[2]);
        else {
            auto drv = store->derivationFromPath(Path(argv[1]));
            impurePaths = tokenizeString<StringSet>(get(drv.env, "__impureHostDeps"));
            impurePaths.insert("/usr/lib/libSystem.dylib");
        }

        std::set<string> allPaths;

        for (auto & path : impurePaths)
            for (auto & p : getPath(path))
                allPaths.insert(p);

        std::cout << "extra-chroot-dirs" << std::endl;
        for (auto & path : allPaths)
            std::cout << path << std::endl;
        std::cout << std::endl;
    });
}
