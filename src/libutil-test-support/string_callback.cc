#include "nix/util/tests/string_callback.hh"

namespace nix::testing {

void observe_string_cb(const char * start, unsigned int n, void * user_data) noexcept
{
    auto user_data_casted = reinterpret_cast<std::string *>(user_data);
    /* This is only for testing. Technically bad_alloc can escape and it would need to
       be propagated but here it's not worth the trouble. */
    *user_data_casted = std::string(start, n);
}

} // namespace nix::testing
