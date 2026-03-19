#pragma once
///@file

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/store/globals.hh"

namespace nix {

/**
 * Scoped guard that enables an experimental feature for the lifetime
 * of the object.
 */
struct EnableExperimentalFeature
{
    std::set<ExperimentalFeature> previous;

    explicit EnableExperimentalFeature(std::string_view feature)
        : previous(experimentalFeatureSettings.experimentalFeatures.get())
    {
        experimentalFeatureSettings.set("extra-experimental-features", std::string{feature});
    }

    ~EnableExperimentalFeature()
    {
        experimentalFeatureSettings.experimentalFeatures.assign(previous);
    }

    EnableExperimentalFeature(const EnableExperimentalFeature &) = delete;
    EnableExperimentalFeature & operator=(const EnableExperimentalFeature &) = delete;
};

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
