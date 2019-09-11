#include "logging.hh"
#include "rust-ffi.hh"

namespace nix {

extern "C" std::exception_ptr * make_error(rust::StringSlice s)
{
    // FIXME: leak
    return new std::exception_ptr(std::make_exception_ptr(Error(std::string(s.ptr, s.size))));
}

}
