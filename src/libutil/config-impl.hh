#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an example of the "impl.hh" pattern. See the
 * contributing guide.
 *
 * One only needs to include this when one is declaring a
 * `BaseClass<CustomType>` setting, or as derived class of such an
 * instantiation.
 */

#include "logging.hh"
#include "args.hh"
#include "config.hh"

namespace nix {

template<> struct Setting<Strings>::trait
{
    static constexpr bool appendable = true;
};
template<> struct Setting<StringSet>::trait
{
    static constexpr bool appendable = true;
};
template<> struct Setting<StringMap>::trait
{
    static constexpr bool appendable = true;
};
template<> struct Setting<std::set<ExperimentalFeature>>::trait
{
    static constexpr bool appendable = true;
};

template<typename T>
struct Setting<T>::trait
{
    static constexpr bool appendable = false;
};

template<typename T>
bool Setting<T>::isAppendable()
{
    return trait::appendable;
}

template<> void Setting<Strings>::appendOrSet(Strings newValue, bool append);
template<> void Setting<StringSet>::appendOrSet(StringSet newValue, bool append);
template<> void Setting<StringMap>::appendOrSet(StringMap newValue, bool append);
template<> void Setting<std::set<ExperimentalFeature>>::appendOrSet(std::set<ExperimentalFeature> newValue, bool append);

template<typename T>
void Setting<T>::appendOrSet(T newValue, bool append)
{
    static_assert(
        !trait::appendable,
        "using default `appendOrSet` implementation with an appendable type");
    assert(!append);

    assign(std::move(newValue));
}

template<typename T>
void Setting<T>::set(const std::string & str, bool append)
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

template<> void Setting<bool>::convertToArg(Args & args, const std::string & category);

template<typename T>
void Setting<T>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = fmt("Set the `%s` setting.", name),
        .category = category,
        .labels = {"value"},
        .handler = {[this](std::string s) { overridden = true; set(s); }},
        .experimentalFeature = experimentalFeature,
    });

    if (isAppendable())
        args.addFlag({
            .longName = "extra-" + name,
            .description = fmt("Append to the `%s` setting.", name),
            .category = category,
            .labels = {"value"},
            .handler = {[this](std::string s) { overridden = true; set(s, true); }},
            .experimentalFeature = experimentalFeature,
        });
}

#define DECLARE_CONFIG_SERIALISER(TY) \
    template<> TY Setting< TY >::parse(const std::string & str) const; \
    template<> std::string Setting< TY >::to_string() const;

DECLARE_CONFIG_SERIALISER(std::string)
DECLARE_CONFIG_SERIALISER(std::optional<std::string>)
DECLARE_CONFIG_SERIALISER(bool)
DECLARE_CONFIG_SERIALISER(Strings)
DECLARE_CONFIG_SERIALISER(StringSet)
DECLARE_CONFIG_SERIALISER(StringMap)
DECLARE_CONFIG_SERIALISER(std::set<ExperimentalFeature>)

template<typename T>
T Setting<T>::parse(const std::string & str) const
{
    static_assert(std::is_integral<T>::value, "Integer required.");

    try {
        return string2IntWithUnitPrefix<T>(str);
    } catch (...) {
        throw UsageError("setting '%s' has invalid value '%s'", name, str);
    }
}

template<typename T>
std::string Setting<T>::to_string() const
{
    static_assert(std::is_integral<T>::value, "Integer required.");

    return std::to_string(value);
}

}
