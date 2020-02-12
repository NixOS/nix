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

}
