#include "string_callback.hh"

namespace nix::testing {

void observe_string_cb(const char * start, unsigned int n, std::string * user_data)
{
    *user_data = std::string(start);
}

}
