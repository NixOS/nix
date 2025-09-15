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
    LibStoreTest()
        : store(openStore({
              .variant =
                  StoreReference::Specified{
                      .scheme = "dummy",
                  },
              .params = {},
          }))
    {
    }

    ref<Store> store;
};

} /* namespace nix */
