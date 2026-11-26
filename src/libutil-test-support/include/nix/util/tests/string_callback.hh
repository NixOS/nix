#pragma once
#include <string>

namespace nix::testing {

void observe_string_cb(const char * start, unsigned int n, void * user_data) noexcept;

inline void * observe_string_cb_data(std::string & out) noexcept
{
    return (void *) &out;
};

#define OBSERVE_STRING(str) nix::testing::observe_string_cb, nix::testing::observe_string_cb_data(str)

} // namespace nix::testing
