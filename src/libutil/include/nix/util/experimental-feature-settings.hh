#pragma once
///@file

#include "nix/util/experimental-features.hh"

namespace nix {

template<template<typename> class R>
struct ExperimentalFeatureSettingsT
{
    R<std::set<ExperimentalFeature>> experimentalFeatures;
};

struct ExperimentalFeatureSettings : ExperimentalFeatureSettingsT<config::PlainValue>
{
    /**
     * Check whether the given experimental feature is enabled.
     */
    bool isEnabled(const ExperimentalFeature &) const;

    /**
     * Require an experimental feature be enabled, throwing an error if it is
     * not.
     */
    void require(const ExperimentalFeature &) const;

    /**
     * `std::nullopt` pointer means no feature, which means there is nothing that could be
     * disabled, and so the function returns true in that case.
     */
    bool isEnabled(const std::optional<ExperimentalFeature> &) const;

    /**
     * `std::nullopt` pointer means no feature, which means there is nothing that could be
     * disabled, and so the function does nothing in that case.
     */
    void require(const std::optional<ExperimentalFeature> &) const;
};

const extern ExperimentalFeatureSettings experimentalFeatureSettingsDefaults;

// FIXME: don't use a global variable.
extern ExperimentalFeatureSettings experimentalFeatureSettings;

} // namespace nix
