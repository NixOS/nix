#pragma once
///@file
#include "nix_api_util.h"

#include <gtest/gtest.h>

class nix_api_util_context : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        nix_libutil_init(NULL);
    }

    nix_api_util_context()
    {
        ctx = nix_c_context_create();
    };

    ~nix_api_util_context() override
    {
        nix_c_context_free(ctx);
        ctx = nullptr;
    }

    nix_c_context * ctx;
};
