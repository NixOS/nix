#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/store/store-api.hh"

namespace nix {

class LibStoreTest : public ::testing::Test {
    public:
        static void SetUpTestSuite() {
            initLibStore();
        }

    protected:
        LibStoreTest()
            : store(openStore("dummy://"))
        { }

        ref<Store> store;
};


} /* namespace nix */
