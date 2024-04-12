#pragma once
#include <string>

namespace nix::testing {

void observe_string_cb(const char * start, unsigned int n, std::string * user_data);

inline void * observe_string_cb_data(std::string & out)
{
    return (void *) &out;
};

#define OBSERVE_STRING(str) \
    (void (*)(const char *, unsigned int, void *)) nix::testing::observe_string_cb, \
        nix::testing::observe_string_cb_data(str)

}
