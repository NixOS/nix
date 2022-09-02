#include "input-accessor.hh"

#include <zip.h>
#include <arpa/inet.h>

namespace nix {

struct cmp_str
{
    bool operator ()(const char * a, const char * b) const
    {
        return std::strcmp(a, b) < 0;
    }
};

struct ZipMember
{
    struct zip_file * p = nullptr;
    ZipMember(struct zip_file * p) : p(p) { }
    ~ZipMember() { if (p) zip_fclose(p); }
    operator zip_file *() { return p; }
};

struct ZipInputAccessor : InputAccessor
{
    CanonPath zipPath;
    struct zip * zipFile = nullptr;

    typedef std::map<const char *, struct zip_stat, cmp_str> Members;
    Members members;

    time_t lastModified = 0;

    ZipInputAccessor(const CanonPath & _zipPath)
        : zipPath(_zipPath)
    {
        int error;
        zipFile = zip_open(zipPath.c_str(), 0, &error);
        if (!zipFile) {
            char errorMsg[1024];
            zip_error_to_str(errorMsg, sizeof errorMsg, error, errno);
            throw Error("couldn't open '%s': %s", zipPath, errorMsg);
        }

        /* Read the index of the zip file and put it in a map.  This
           is unfortunately necessary because libzip's lookup
           functions are O(n) time. */
        struct zip_stat sb;
        zip_uint64_t nrEntries = zip_get_num_entries(zipFile, 0);
        for (zip_uint64_t n = 0; n < nrEntries; ++n) {
            if (zip_stat_index(zipFile, n, 0, &sb))
                throw Error("couldn't stat archive member #%d in '%s': %s", n, zipPath, zip_strerror(zipFile));

            /* Get the timestamp of this file. */
            #if 0
            if (sb.valid & ZIP_STAT_MTIME)
                lastModified = std::max(lastModified, sb.mtime);
            #endif
            auto nExtra = zip_file_extra_fields_count(zipFile, n, ZIP_FL_CENTRAL);
            for (auto i = 0; i < nExtra; ++i) {
                zip_uint16_t id, len;
                auto extra = zip_file_extra_field_get(zipFile, i, 0, &id, &len, ZIP_FL_CENTRAL);
                if (id == 0x5455 && len >= 5)
                    lastModified = std::max(lastModified, (time_t) readLittleEndian<uint32_t>((unsigned char *) extra + 1));
            }

            auto slash = strchr(sb.name, '/');
            if (!slash) continue;
            members.emplace(slash, sb);
        }

        #if 0
        /* Sigh, libzip returns a local time, so convert to Unix
           time. */
        if (lastModified) {
            struct tm tm;
            localtime_r(&lastModified, &tm);
            lastModified = timegm(&tm);
        }
        #endif
    }

    ~ZipInputAccessor()
    {
        if (zipFile) zip_close(zipFile);
    }

    std::string _readFile(const CanonPath & path)
    {
        auto i = members.find(((std::string) path.abs()).c_str());
        if (i == members.end())
            throw Error("file '%s' does not exist", showPath(path));

        ZipMember member(zip_fopen_index(zipFile, i->second.index, 0));
        if (!member)
            throw Error("couldn't open archive member '%s': %s",
                showPath(path), zip_strerror(zipFile));

        std::string buf(i->second.size, 0);
        if (zip_fread(member, buf.data(), i->second.size) != (zip_int64_t) i->second.size)
            throw Error("couldn't read archive member '%s' in '%s'", path, zipPath);

        return buf;
    }

    std::string readFile(const CanonPath & path) override
    {
        if (lstat(path).type != tRegular)
            throw Error("file '%s' is not a regular file", path);

        return _readFile(path);
    }

    bool pathExists(const CanonPath & path) override
    {
        return
            members.find(path.c_str()) != members.end()
            || members.find(((std::string) path.abs() + "/").c_str()) != members.end();
    }

    Stat lstat(const CanonPath & path) override
    {
        if (path.isRoot())
            return Stat { .type = tDirectory };

        Type type = tRegular;
        bool isExecutable = false;

        auto i = members.find(path.c_str());
        if (i == members.end()) {
            i = members.find(((std::string) path.abs() + "/").c_str());
            type = tDirectory;
        }
        if (i == members.end())
            throw Error("file '%s' does not exist", showPath(path));

        // FIXME: cache this
        zip_uint8_t opsys;
        zip_uint32_t attributes;
        if (zip_file_get_external_attributes(zipFile, i->second.index, ZIP_FL_UNCHANGED, &opsys, &attributes) == -1)
            throw Error("couldn't get external attributes of '%s': %s",
                showPath(path), zip_strerror(zipFile));

        switch (opsys) {
        case ZIP_OPSYS_UNIX:
            auto t = (attributes >> 16) & 0770000;
            switch (t) {
            case 0040000: type = tDirectory; break;
            case 0100000:
                type = tRegular;
                isExecutable = (attributes >> 16) & 0000100;
                break;
            case 0120000: type = tSymlink; break;
            default:
                throw Error("file '%s' has unsupported type %o", showPath(path), t);
            }
            break;
        }

        return Stat { .type = type, .isExecutable = isExecutable };
    }

    DirEntries readDirectory(const CanonPath & _path) override
    {
        std::string path(_path.abs());
        if (path != "/") path += "/";

        auto i = members.find(path.c_str());
        if (i == members.end())
            throw Error("directory '%s' does not exist", showPath(_path));

        ++i;

        DirEntries entries;

        for (; i != members.end() && strncmp(i->first, path.c_str(), path.size()) == 0; ++i) {
            auto start = i->first + path.size();
            auto slash = strchr(start, '/');
            if (slash && strcmp(slash, "/") != 0) continue;
            auto name = slash ? std::string(start, slash - start) : std::string(start);
            entries.emplace(name, std::nullopt);
        }

        return entries;
    }

    std::string readLink(const CanonPath & path) override
    {
        if (lstat(path).type != tSymlink)
            throw Error("file '%s' is not a symlink", showPath(path));

        return _readFile(path);
    }

    std::optional<time_t> getLastModified() override
    {
        return lastModified;
    }
};

ref<InputAccessor> makeZipInputAccessor(const CanonPath & path)
{
    return make_ref<ZipInputAccessor>(path);
}

}
