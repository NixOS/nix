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

    inline void assert_ctx_ok()
    {
        if (nix_err_code(ctx) == NIX_OK) {
            return;
        }
        unsigned int n;
        const char * p = nix_err_msg(nullptr, ctx, &n);
        std::string msg(p, n);
        throw std::runtime_error(std::string("nix_err_code(ctx) != NIX_OK, message: ") + msg);
    }

    inline void assert_ctx_err()
    {
        if (nix_err_code(ctx) != NIX_OK) {
            return;
        }
        throw std::runtime_error("Got NIX_OK, but expected an error!");
    }
};

}
