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
    void SetUp() override
    {
        ctx = nix_c_context_create();
    };
    void TearDown() override
    {
        nix_c_context_free(ctx);
        ctx = nullptr;
    }
    nix_c_context * ctx;
};
