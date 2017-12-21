#include "command.hh"
#include "store-api.hh"
#include "fs-accessor.hh"
#include "nar-accessor.hh"

#define FUSE_USE_VERSION 30
#include <fuse.h>

#include <cstring>

using namespace nix;

std::shared_ptr<Store> store;
std::shared_ptr<FSAccessor> accessor;

static int op_getattr(const char * path_, struct stat * stbuf)
{
    try {

        Path path(path_);

        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_uid = 0;
        stbuf->st_gid = 0;
        stbuf->st_nlink = 1;

        if (path == "/") {
            stbuf->st_mode = S_IFDIR | 0111;
        } else {
            auto st = accessor->stat(store->storeDir + path);

            switch (st.type) {
            case FSAccessor::tRegular:
                stbuf->st_mode = S_IFREG | (st.isExecutable ? 0555 : 0444);
                stbuf->st_size = st.fileSize;
                break;
            case FSAccessor::tSymlink:
                stbuf->st_mode = S_IFLNK | 0777;
                break;
            case FSAccessor::tDirectory:
                stbuf->st_mode = S_IFDIR | 0555;
                break;
            default:
                return -ENOENT;
            }
        }

        return 0;

    } catch (...) {
        ignoreException();
        return -EIO;
    }
}

static int op_readdir(const char * path_, void * buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info * fi)
{
    try {

        Path path(path_);

        if (path == "/")
            /* FIXME: could use queryAllValidPaths(), but it will be
               superslow for binary caches, and won't include name
               parts. */
            return 0;

        auto st = accessor->stat(store->storeDir + path);
        if (st.type == FSAccessor::tMissing) return -ENOENT;
        if (st.type != FSAccessor::tDirectory) return -ENOTDIR;

        for (auto & entry : accessor->readDirectory(store->storeDir + path))
            filler(buf, entry.c_str(), nullptr, 0);

        return 0;

    } catch (...) {
        ignoreException();
        return -EIO;
    }
}

static int op_open(const char * path_, struct fuse_file_info * fi)
{
    try {

        Path path(path_);

        auto st = accessor->stat(store->storeDir + path);
        if (st.type == FSAccessor::tMissing) return -ENOENT;
        if (st.type == FSAccessor::tDirectory) return -EISDIR;
        if (st.type != FSAccessor::tRegular) return -EINVAL;

        return 0;

    } catch (...) {
        ignoreException();
        return -EIO;
    }
}

static int op_read(const char * path_, char * buf, size_t size, off_t offset,
    struct fuse_file_info * fi)
{
    try {

        Path path(path_);

        // FIXME: absolutely need to cache this and/or provide random
        // access.

        auto s = accessor->readFile(store->storeDir + path);

        if (offset >= (off_t) s.size()) return 0;

        if (offset + size > s.size())
            size = s.size() - offset;

        memcpy(buf, s.data() + offset, size);

        return size;

    } catch (...) {
        ignoreException();
        return -EIO;
    }
}

static int op_readlink(const char * path_, char * buf, size_t size)
{
    try {

        Path path(path_);

        auto st = accessor->stat(store->storeDir + path);
        if (st.type == FSAccessor::tMissing) return -ENOENT;
        if (st.type != FSAccessor::tSymlink) return -EINVAL;

        auto s = accessor->readLink(store->storeDir + path);

        if (s.size() >= size) return ENAMETOOLONG; // FIXME

        strncpy(buf, s.c_str(), size);

        return 0;

    } catch (...) {
        ignoreException();
        return -EIO;
    }
}

struct CmdMountStore : StoreCommand
{
    Path mountPoint;

    CmdMountStore()
    {
        expectArg("mount-point", &mountPoint);
    }

    std::string name() override
    {
        return "mount-store";
    }

    std::string description() override
    {
        return "mount a Nix store as a FUSE file system";
    }

    void run(ref<Store> store) override
    {
        ::store = store;
        accessor = store->getFSAccessor();

        Strings fuseArgs = { "nix", mountPoint, "-o", "debug" };
        auto fuseArgs2 = stringsToCharPtrs(fuseArgs);

        struct fuse * fuse;
        char * mountpoint;
        int multithreaded;

        fuse_operations oper;
        memset(&oper, 0, sizeof(oper));
        oper.getattr = op_getattr;
        oper.readdir = op_readdir;
        oper.open = op_open;
        oper.read = op_read;
        oper.readlink = op_readlink;

        fuse = fuse_setup(fuseArgs2.size() - 1, fuseArgs2.data(),
            &oper, sizeof(oper),
            &mountpoint, &multithreaded, nullptr);
        if (!fuse) throw Error("FUSE setup failed");

        if (multithreaded)
            fuse_loop_mt(fuse);
        else
            fuse_loop(fuse);

        fuse_teardown(fuse, mountpoint);
    }
};

static RegisterCommand r(make_ref<CmdMountStore>());
