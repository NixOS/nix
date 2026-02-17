#pragma once
///@file

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/store/globals.hh"
#include "nix/store/tests/test-main.hh"

namespace nix {

class LibStoreTest : public virtual ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        initLibStore(false);
    }

    Settings settings = getTestSettings();

protected:
    LibStoreTest(auto && makeStore)
        : store(makeStore(settings))
    {
    }

    LibStoreTest()
        : LibStoreTest([](auto & settings) { return openStore(settings, "dummy://"); })
    {
    }

    ref<Store> store;
};

} /* namespace nix */
