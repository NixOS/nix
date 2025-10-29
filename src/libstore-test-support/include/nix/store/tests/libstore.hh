#pragma once
///@file

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/store/globals.hh"

namespace nix {

class LibStoreTest : public virtual ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        initLibStore(false);
    }

protected:
    LibStoreTest(ref<Store> store)
        : store(std::move(store))
    {
    }

    LibStoreTest()
        : LibStoreTest(openStore("dummy://"))
    {
    }

    ref<Store> store;
};

} /* namespace nix */
