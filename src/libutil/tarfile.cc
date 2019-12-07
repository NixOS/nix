#include "rust-ffi.hh"
#include "compression.hh"
#include <archive.h>
#include <archive_entry.h>
#include "finally.hh"

extern "C" {
    rust::Result<std::tuple<>> *
    unpack_tarfile(rust::Source source, rust::StringSlice dest_dir);
}

namespace nix {

std::shared_ptr<struct archive> archive_read_ptr() {
    return std::shared_ptr<struct archive>(archive_read_new(),
        [](auto p) {
            archive_read_close(p);
            archive_read_free(p);
        });
}
void archive_read_open_source(std::shared_ptr<struct archive> a, Source& s, unsigned int bufsize = 1024) {
    std::shared_ptr<unsigned char> buffer((unsigned char*)malloc(bufsize), [](auto p) { free(p); });
    typedef struct {
        decltype(buffer) buf;
        Source& src;
        unsigned int bs;
    } St;
    St* state = new St({buffer, s, bufsize});
    if (archive_read_open(a.get(), state,
            NULL /* open */,
            ([] (struct archive*, void* sptr, const void** buf) -> long int {
                 St& s = *(static_cast<St*>(sptr));
                 *buf = s.buf.get();
                 try {
                     return s.src.read(s.buf.get(), s.bs);
                 } catch (EndOfFile &) {
                     return 0;
                 }
                 /* TODO: I don't know what happens if anything else is thrown here */
             }), [] (struct archive*, void* sptr) {
                     delete static_cast<St*>(sptr);
                     return ARCHIVE_OK;
                 })) {
        throw Error("archive is corrupt (%s)", archive_error_string(a.get()));
    }
}
std::shared_ptr<struct archive> archive_write_ptr() {
    return std::shared_ptr<struct archive>(archive_write_disk_new(),
        [](auto p) {
            archive_write_close(p);
            archive_write_free(p);
        });
}
static void copy_data(std::shared_ptr<struct archive> ar, std::shared_ptr<struct archive> aw)
{
  int r;
  const void *buff;
  size_t size;
  la_int64_t offset;

  for (;;) {
      r = archive_read_data_block(ar.get(), &buff, &size, &offset);
      if (r == ARCHIVE_EOF) return;
      if (r != ARCHIVE_OK) {
          throw Error("archive is corrupt (%s)", archive_error_string(ar.get()));
      }
      r = archive_write_data_block(aw.get(), buff, size, offset);
      if (r != ARCHIVE_OK) {
          throw Error("could not write archive output (%s)", archive_error_string(aw.get()));
      }
  }
}

static void extract_archive(std::shared_ptr<struct archive> a, const Path & destDir) {
    char * cwd = getcwd(0, 0);
    if (!cwd) throw SysError("getting current directory");
    Finally freeCwd([&]() { chdir(cwd); free(cwd); });
    int r = chdir(destDir.c_str());
    if (r != 0) throw SysError("setting directory to tar output path");
    struct archive_entry *entry;
    int flags = 0;
    auto ext = archive_write_ptr();
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_FFLAGS;
    archive_write_disk_set_options(ext.get(), flags);
    archive_write_disk_set_standard_lookup(ext.get());
    for(;;) {
        r = archive_read_next_header(a.get(), &entry);
        if (r == ARCHIVE_EOF) break;
        if (r == ARCHIVE_WARN) {
            std::cerr << "warning: " << archive_error_string(a.get());
        } else if (r < ARCHIVE_WARN) {
            throw Error("archive is corrupt (%s)", archive_error_string(a.get()));
        }
        r = archive_write_header(ext.get(), entry);
        if (r != ARCHIVE_OK) {
            throw Error("could not write archive output (%s)", archive_error_string(ext.get()));
        }
        copy_data(a, ext);
        archive_write_finish_entry(ext.get());
    }
    //if (r != 0) throw SysError("resetting directory after archive extraction");
}
void unpackTarfile(Source & source, const Path & destDir)
{
    auto a = archive_read_ptr();
    archive_read_support_filter_all(a.get());
    archive_read_support_format_all(a.get());
    archive_read_open_source(a, source);
    createDirs(destDir);
    extract_archive(a, destDir);
}
void unpackTarfile(const Path & tarFile, const Path & destDir)
{
    auto a = archive_read_ptr();
    archive_read_support_filter_all(a.get());
    archive_read_support_format_all(a.get());
    int r = archive_read_open_filename(a.get(), tarFile.c_str(), 16384);
    if (r != ARCHIVE_OK) {
        throw Error("archive is corrupt (%s)", archive_error_string(a.get()));
    }
    createDirs(destDir);
    extract_archive(a, destDir);
}

}
