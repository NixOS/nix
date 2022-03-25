#include <archive.h>
#include <archive_entry.h>

#include "serialise.hh"
#include "tarfile.hh"

namespace nix {

static int callback_open(struct archive *, void * self)
{
    return ARCHIVE_OK;
}

static ssize_t callback_read(struct archive * archive, void * _self, const void * * buffer)
{
    auto self = (TarArchive *) _self;
    *buffer = self->buffer.data();

    try {
        return self->source->read((char *) self->buffer.data(), 4096);
    } catch (EndOfFile &) {
        return 0;
    } catch (std::exception & err) {
        archive_set_error(archive, EIO, "Source threw exception: %s", err.what());
        return -1;
    }
}

static int callback_close(struct archive *, void * self)
{
    return ARCHIVE_OK;
}

void TarArchive::check(int err, const std::string & reason)
{
    if (err == ARCHIVE_EOF)
        throw EndOfFile("reached end of archive");
    else if (err != ARCHIVE_OK)
        throw Error(reason, archive_error_string(this->archive));
}

TarArchive::TarArchive(Source & source, bool raw) : buffer(4096)
{
    this->archive = archive_read_new();
    this->source = &source;

    if (!raw) {
        archive_read_support_filter_all(archive);
        archive_read_support_format_all(archive);
    } else {
        archive_read_support_filter_all(archive);
        archive_read_support_format_raw(archive);
        archive_read_support_format_empty(archive);
    }
    check(archive_read_open(archive, (void *)this, callback_open, callback_read, callback_close), "Failed to open archive (%s)");
}


TarArchive::TarArchive(const Path & path)
{
    this->archive = archive_read_new();

    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);
    check(archive_read_open_filename(archive, path.c_str(), 16384), "failed to open archive: %s");
}

void TarArchive::close()
{
    check(archive_read_close(this->archive), "Failed to close archive (%s)");
}

TarArchive::~TarArchive()
{
    if (this->archive) archive_read_free(this->archive);
}

static void extract_archive(TarArchive & archive, const Path & destDir)
{
    int flags = ARCHIVE_EXTRACT_FFLAGS
        | ARCHIVE_EXTRACT_PERM
        | ARCHIVE_EXTRACT_TIME
        | ARCHIVE_EXTRACT_SECURE_SYMLINKS
        | ARCHIVE_EXTRACT_SECURE_NODOTDOT;

    for (;;) {
        struct archive_entry * entry;
        int r = archive_read_next_header(archive.archive, &entry);
        if (r == ARCHIVE_EOF) break;
        auto name = archive_entry_pathname(entry);
        if (!name)
            throw Error("cannot get archive member name: %s", archive_error_string(archive.archive));
        if (r == ARCHIVE_WARN)
            warn(archive_error_string(archive.archive));
        else
            archive.check(r);

        archive_entry_copy_pathname(entry,
            (destDir + "/" + name).c_str());

        // Patch hardlink path
        const char *original_hardlink = archive_entry_hardlink(entry);
        if (original_hardlink) {
            archive_entry_copy_hardlink(entry,
                (destDir + "/" + original_hardlink).c_str());
        }

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
