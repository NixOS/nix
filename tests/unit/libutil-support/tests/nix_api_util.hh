#pragma once
///@file
#include "nix_api_util.h"

#include <gtest/gtest.h>

namespace nixC {

class nix_api_util_context : public ::testing::Test
{
protected:

    nix_api_util_context()
    {
        ctx = nix_c_context_create();
        nix_libutil_init(ctx);
    };

    ~nix_api_util_context() override
    {
        nix_c_context_free(ctx);
        ctx = nullptr;
    }

    nix_c_context * ctx;
};
}
