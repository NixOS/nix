#include "logging.hh"
#include "rust-ffi.hh"

extern "C" std::exception_ptr * make_error(rust::StringSlice s)
{
    return new std::exception_ptr(std::make_exception_ptr(nix::Error(std::string(s.ptr, s.size))));
}

extern "C" void destroy_error(std::exception_ptr * ex)
{
    free(ex);
}

namespace rust {

std::ostream & operator << (std::ostream & str, const String & s)
{
    str << (std::string_view) s;
    return str;
}

size_t Source::sourceWrapper(void * _this, rust::Slice<uint8_t> data)
{
    try {
        // FIXME: how to propagate exceptions?
        auto n = ((nix::Source *) _this)->read((unsigned char *) data.ptr, data.size);
        return n;
    } catch (...) {
        abort();
    }
}

}
