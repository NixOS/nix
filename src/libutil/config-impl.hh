#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 *
 * One only needs to include this when one is declaring a
 * `BaseClass<CustomType>` setting, or as derived class of such an
 * instantiation.
 */

#include "config.hh"

namespace nix {

template<> struct BaseSetting<Strings>::trait
{
    static constexpr bool appendable = true;
};
template<> struct BaseSetting<StringSet>::trait
{
    static constexpr bool appendable = true;
};
template<> struct BaseSetting<StringMap>::trait
{
    static constexpr bool appendable = true;
};
template<> struct BaseSetting<std::set<ExperimentalFeature>>::trait
{
    static constexpr bool appendable = true;
};

template<typename T>
struct BaseSetting<T>::trait
{
    static constexpr bool appendable = false;
};

template<typename T>
bool BaseSetting<T>::isAppendable()
{
    return trait::appendable;
}

template<> void BaseSetting<Strings>::appendOrSet(Strings && newValue, bool append);
template<> void BaseSetting<StringSet>::appendOrSet(StringSet && newValue, bool append);
template<> void BaseSetting<StringMap>::appendOrSet(StringMap && newValue, bool append);
template<> void BaseSetting<std::set<ExperimentalFeature>>::appendOrSet(std::set<ExperimentalFeature> && newValue, bool append);

template<typename T>
void BaseSetting<T>::appendOrSet(T && newValue, bool append)
{
    static_assert(!trait::appendable, "using default `appendOrSet` implementation with an appendable type");
    assert(!append);
    value = std::move(newValue);
}

template<typename T>
void BaseSetting<T>::set(const std::string & str, bool append)
{
    if (experimentalFeatureSettings.isEnabled(experimentalFeature))
        appendOrSet(parse(str), append);
    else {
        assert(experimentalFeature);
        warn("Ignoring setting '%s' because experimental feature '%s' is not enabled",
            name,
            showExperimentalFeature(*experimentalFeature));
    }
}

}
