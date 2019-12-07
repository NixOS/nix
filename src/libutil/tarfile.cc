#include "rust-ffi.hh"
#include "compression.hh"
#include <archive.h>
#include <archive_entry.h>
#include "finally.hh"

namespace nix {

struct TarArchive {
    struct archive *archive;
    Source *source;
    unsigned char buffer[4096];

    void check(int err, const char *reason = "Failed to extract archive (%s)") {
        if (err == ARCHIVE_EOF)
            throw EndOfFile("reached end of archive");
        else if (err != ARCHIVE_OK)
            throw Error(reason, archive_error_string(this->archive));
    }

    TarArchive(Source& source) {
        this->archive = archive_read_new();
        this->source = &source;

        archive_read_support_filter_all(archive);
        archive_read_support_format_all(archive);
        check(archive_read_open(archive, (void *)this, TarArchive::callback_open, TarArchive::callback_read, TarArchive::callback_close), "Failed to open archive (%s)");
    }

    TarArchive(const Path &path) {
        this->archive = archive_read_new();

        archive_read_support_filter_all(archive);
        archive_read_support_format_all(archive);
        check(archive_read_open_filename(archive, path.c_str(), 16384), "Failed to open archive (%s)");
    }

    void close() {
        check(archive_read_close(archive), "Failed to close archive (%s)");
    }

    ~TarArchive() {
        if (this->archive) archive_read_free(this->archive);
    }

private:
    static int callback_open(struct archive *, void *self) {
        return ARCHIVE_OK;
    }
    
    static ssize_t callback_read(struct archive *archive, void *_self, const void **buffer) {
        TarArchive *self = (TarArchive *)_self; 
        *buffer = self->buffer;

        try {
            return self->source->read(self->buffer, 4096);
        } catch (EndOfFile &) {
            return 0;
        } catch (std::exception &err) {
            archive_set_error(archive, EIO, "Source threw exception: %s", err.what());

            return -1;
        }
    }

    static int callback_close(struct archive *, void *self) {
        return ARCHIVE_OK;
    }
};

struct PushD {
    char * oldDir;

    PushD(const std::string &newDir)  {
        oldDir = getcwd(0, 0);
        if (!oldDir) throw SysError("getcwd");
        int r = chdir(newDir.c_str());
        if (r != 0) throw SysError("changing directory to tar output path");
    }

    ~PushD() {
        int r = chdir(oldDir);
        free(oldDir);
        if (r != 0)
            std::cerr << "warning: popd failed to chdir";
        /* can't throw out of a destructor */
    }
};

static void extract_archive(TarArchive &archive, const Path & destDir) {
    // need to chdir back *after* archive closing
    PushD newDir(destDir);
    struct archive_entry *entry;
    int flags = ARCHIVE_EXTRACT_FFLAGS | ARCHIVE_EXTRACT_PERM;

    for(;;) {
        int r = archive_read_next_header(archive.archive, &entry);
        if (r == ARCHIVE_EOF) break;
        else if (r == ARCHIVE_WARN)
            std::cerr << "warning: " << archive_error_string(archive.archive) << std::endl;
        else
            archive.check(r);

        archive.check(archive_read_extract(archive.archive, entry, flags));
    }

    archive.close();
}

void unpackTarfile(Source & source, const Path & destDir)
{
    auto archive = TarArchive(source);

    createDirs(destDir);
    extract_archive(archive, destDir);
}

void unpackTarfile(const Path & tarFile, const Path & destDir)
{
    auto archive = TarArchive(tarFile);

    createDirs(destDir);
    extract_archive(archive, destDir);
}

}
