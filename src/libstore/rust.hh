#include "serialise.hh"
#include "rust-ffi.hh"

extern "C" {
    rust::CBox2<rust::Result<std::tuple<>>> unpack_tarfile(rust::Source source, rust::StringSlice dest_dir);
}
