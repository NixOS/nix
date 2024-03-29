#include <archive.h>
#include <archive_entry.h>

#include "finally.hh"
#include "serialise.hh"
#include "tarfile.hh"
#include "file-system.hh"

namespace nix {

namespace {

int callback_open(struct archive *, void * self)
{
    return ARCHIVE_OK;
}

ssize_t callback_read(struct archive * archive, void * _self, const void ** buffer)
{
    auto self = (TarArchive *) _self;
    *buffer = self->buffer.data();

    try {
        return self->source->read((char *) self->buffer.data(), self->buffer.size());
    } catch (EndOfFile &) {
        return 0;
    } catch (std::exception & err) {
        archive_set_error(archive, EIO, "Source threw exception: %s", err.what());
        return -1;
    }
}

int callback_close(struct archive *, void * self)
{
    return ARCHIVE_OK;
}

void checkLibArchive(archive * archive, int err, const std::string & reason)
{
    if (err == ARCHIVE_EOF)
        throw EndOfFile("reached end of archive");
    else if (err != ARCHIVE_OK)
        throw Error(reason, archive_error_string(archive));
}

constexpr auto defaultBufferSize = std::size_t{65536};
}

void TarArchive::check(int err, const std::string & reason)
{
    checkLibArchive(archive, err, reason);
}

/// @brief Get filter_code from its name.
///
/// libarchive does not provide a convenience function like archive_write_add_filter_by_name but for reading.
/// Instead it's necessary to use this kludge to convert method -> code and
/// then use archive_read_support_filter_by_code. Arguably this is better than
/// hand-rolling the equivalent function that is better implemented in libarchive.
int getArchiveFilterCodeByName(const std::string & method)
{
    auto * ar = archive_write_new();
    auto cleanup = Finally{[&ar]() { checkLibArchive(ar, archive_write_close(ar), "failed to close archive: %s"); }};
    auto err = archive_write_add_filter_by_name(ar, method.c_str());
    checkLibArchive(ar, err, "failed to get libarchive filter by name: %s");
    auto code = archive_filter_code(ar, 0);
    return code;
}

TarArchive::TarArchive(Source & source, bool raw, std::optional<std::string> compression_method)
    : archive{archive_read_new()}
    , source{&source}
    , buffer(defaultBufferSize)
{
    if (!compression_method) {
        archive_read_support_filter_all(archive);
    } else {
        archive_read_support_filter_by_code(archive, getArchiveFilterCodeByName(*compression_method));
    }

    if (!raw) {
        archive_read_support_format_all(archive);
    } else {
        archive_read_support_format_raw(archive);
        archive_read_support_format_empty(archive);
    }

    archive_read_set_option(archive, NULL, "mac-ext", NULL);
    check(
        archive_read_open(archive, (void *) this, callback_open, callback_read, callback_close),
        "Failed to open archive (%s)");
}

TarArchive::TarArchive(const Path & path)
    : archive{archive_read_new()}
    , buffer(defaultBufferSize)
{
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);
    archive_read_set_option(archive, NULL, "mac-ext", NULL);
    check(archive_read_open_filename(archive, path.c_str(), 16384), "failed to open archive: %s");
}

void TarArchive::close()
{
    check(archive_read_close(this->archive), "Failed to close archive (%s)");
}

TarArchive::~TarArchive()
{
    if (this->archive)
        archive_read_free(this->archive);
}

static void extract_archive(TarArchive & archive, const Path & destDir)
{
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT;

    for (;;) {
        struct archive_entry * entry;
        int r = archive_read_next_header(archive.archive, &entry);
        if (r == ARCHIVE_EOF)
            break;
        auto name = archive_entry_pathname(entry);
        if (!name)
            throw Error("cannot get archive member name: %s", archive_error_string(archive.archive));
        if (r == ARCHIVE_WARN)
            warn(archive_error_string(archive.archive));
        else
            archive.check(r);

        archive_entry_copy_pathname(entry, (destDir + "/" + name).c_str());

        // sources can and do contain dirs with no rx bits
        if (archive_entry_filetype(entry) == AE_IFDIR && (archive_entry_mode(entry) & 0500) != 0500)
            archive_entry_set_mode(entry, archive_entry_mode(entry) | 0500);

        // Patch hardlink path
        const char * original_hardlink = archive_entry_hardlink(entry);
        if (original_hardlink) {
            archive_entry_copy_hardlink(entry, (destDir + "/" + original_hardlink).c_str());
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

time_t unpackTarfileToSink(TarArchive & archive, FileSystemObjectSink & parseSink)
{
    time_t lastModified = 0;

    for (;;) {
        // FIXME: merge with extract_archive
        struct archive_entry * entry;
        int r = archive_read_next_header(archive.archive, &entry);
        if (r == ARCHIVE_EOF)
            break;
        auto path = archive_entry_pathname(entry);
        if (!path)
            throw Error("cannot get archive member name: %s", archive_error_string(archive.archive));
        if (r == ARCHIVE_WARN)
            warn(archive_error_string(archive.archive));
        else
            archive.check(r);

        lastModified = std::max(lastModified, archive_entry_mtime(entry));

        switch (archive_entry_filetype(entry)) {

        case AE_IFDIR:
            parseSink.createDirectory(path);
            break;

        case AE_IFREG: {
            parseSink.createRegularFile(path, [&](auto & crf) {
                if (archive_entry_mode(entry) & S_IXUSR)
                    crf.isExecutable();

                while (true) {
                    std::vector<unsigned char> buf(128 * 1024);
                    auto n = archive_read_data(archive.archive, buf.data(), buf.size());
                    if (n < 0)
                        throw Error("cannot read file '%s' from tarball", path);
                    if (n == 0)
                        break;
                    crf(std::string_view{
                        (const char *) buf.data(),
                        (size_t) n,
                    });
                }
            });

            break;
        }

        case AE_IFLNK: {
            auto target = archive_entry_symlink(entry);

            parseSink.createSymlink(path, target);

            break;
        }

        default:
            throw Error("file '%s' in tarball has unsupported file type", path);
        }
    }

    return lastModified;
}

}
