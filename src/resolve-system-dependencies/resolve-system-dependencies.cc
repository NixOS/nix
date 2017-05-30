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

void writeCacheFile(const Path & file, std::set<string> & deps)
{
    std::ofstream fp;
    fp.open(file);
    for (auto & d : deps)
        fp << d << "\n";
    fp.close();
}

std::string findDylibName(bool should_swap, ptrdiff_t dylib_command_start)
{
    struct dylib_command *dylc = (struct dylib_command*)dylib_command_start;
    return std::string((char*)(dylib_command_start + DO_SWAP(should_swap, dylc->dylib.name.offset)));
}

std::set<std::string> runResolver(const Path & filename)
{
    AutoCloseFD fd = open(filename.c_str(), O_RDONLY);
    if (!fd)
        throw SysError("opening ‘%s’", filename);

    struct stat st;
    if (fstat(fd.get(), &st))
        throw SysError("statting ‘%s’", filename);

    if (st.st_size < sizeof(mach_header_64)) {
        printError("file ‘%s’ is too short for a MACH binary", filename);
        return {};
    }

    void *obj = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd.get(), 0);
    if (!obj)
        throw SysError("mmapping ‘%s’", filename);

    ptrdiff_t mach64_offset = 0;

    uint32_t magic = ((struct mach_header_64*) obj)->magic;
    if (magic == FAT_CIGAM || magic == FAT_MAGIC) {
        bool should_swap = magic == FAT_CIGAM;
        uint32_t narches = DO_SWAP(should_swap, ((struct fat_header*)obj)->nfat_arch);

        for (uint32_t iter = 0; iter < narches; iter++) {
            ptrdiff_t header_offset = (ptrdiff_t)obj + sizeof(struct fat_header) * (iter + 1);
            struct fat_arch* arch = (struct fat_arch*)header_offset;
            if (DO_SWAP(should_swap, arch->cputype) == CPU_TYPE_X86_64) {
                mach64_offset = (ptrdiff_t)DO_SWAP(should_swap, arch->offset);
                break;
            }
        }
        if (mach64_offset == 0) {
            printError(format("Could not find any mach64 blobs in file ‘%1%’, continuing...") % filename);
            return {};
        }
    } else if (magic == MH_MAGIC_64 || magic == MH_CIGAM_64) {
        mach64_offset = 0;
    } else {
        printError(format("Object file has unknown magic number ‘%1%’, skipping it...") % magic);
        return {};
    }

    ptrdiff_t mach_header_offset = (ptrdiff_t)obj + mach64_offset;
    struct mach_header_64 *m_header = (struct mach_header_64 *)mach_header_offset;

    bool should_swap = magic == MH_CIGAM_64;
    ptrdiff_t cmd_offset = mach_header_offset + sizeof(struct mach_header_64);

    std::set<string> libs;
    for(uint32_t i = 0; i < DO_SWAP(should_swap, m_header->ncmds); i++) {
        struct load_command *cmd = (struct load_command*)cmd_offset;
        switch(DO_SWAP(should_swap, cmd->cmd)) {
            case LC_LOAD_UPWARD_DYLIB:
            case LC_LOAD_DYLIB:
            case LC_REEXPORT_DYLIB:
                libs.insert(findDylibName(should_swap, cmd_offset));
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
        throw SysError("getting attributes of path ‘%1%’", path);

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

    writeCacheFile(cacheFile, paths);

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

        auto drv = store->derivationFromPath(Path(argv[1]));
        Strings impurePaths = tokenizeString<Strings>(get(drv.env, "__impureHostDeps"));

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
