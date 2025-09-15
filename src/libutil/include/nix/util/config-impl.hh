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

#include "nix/util/util.hh"
#include "nix/util/configuration.hh"
#include "nix/util/args.hh"
#include "nix/util/logging.hh"

namespace nix {

template<>
struct BaseSetting<Strings>::trait
{
    static constexpr bool appendable = true;
};

template<>
struct BaseSetting<StringSet>::trait
{
    static constexpr bool appendable = true;
};

template<>
struct BaseSetting<StringMap>::trait
{
    static constexpr bool appendable = true;
};

template<>
struct BaseSetting<std::set<ExperimentalFeature>>::trait
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

template<>
void BaseSetting<Strings>::appendOrSet(Strings newValue, bool append);
template<>
void BaseSetting<StringSet>::appendOrSet(StringSet newValue, bool append);
template<>
void BaseSetting<StringMap>::appendOrSet(StringMap newValue, bool append);
template<>
void BaseSetting<std::set<ExperimentalFeature>>::appendOrSet(std::set<ExperimentalFeature> newValue, bool append);

template<typename T>
void BaseSetting<T>::appendOrSet(T newValue, bool append)
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
        warn(
            "Ignoring setting '%s' because experimental feature '%s' is not enabled",
            name,
            showExperimentalFeature(*experimentalFeature));
    }
}

template<>
void BaseSetting<bool>::convertToArg(Args & args, const std::string & category);

template<typename T>
void BaseSetting<T>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .aliases = aliases,
        .description = fmt("Set the `%s` setting.", name),
        .category = category,
        .labels = {"value"},
        .handler = {[this](std::string s) {
            overridden = true;
            set(s);
        }},
        .experimentalFeature = experimentalFeature,
    });

    if (isAppendable())
        args.addFlag({
            .longName = "extra-" + name,
            .aliases = aliases,
            .description = fmt("Append to the `%s` setting.", name),
            .category = category,
            .labels = {"value"},
            .handler = {[this](std::string s) {
                overridden = true;
                set(s, true);
            }},
            .experimentalFeature = experimentalFeature,
        });
}

#define DECLARE_CONFIG_SERIALISER(TY)                         \
    template<>                                                \
    TY BaseSetting<TY>::parse(const std::string & str) const; \
    template<>                                                \
    std::string BaseSetting<TY>::to_string() const;

DECLARE_CONFIG_SERIALISER(std::string)
DECLARE_CONFIG_SERIALISER(std::optional<std::string>)
DECLARE_CONFIG_SERIALISER(bool)
DECLARE_CONFIG_SERIALISER(Strings)
DECLARE_CONFIG_SERIALISER(StringSet)
DECLARE_CONFIG_SERIALISER(StringMap)
DECLARE_CONFIG_SERIALISER(std::set<ExperimentalFeature>)

template<typename T>
T BaseSetting<T>::parse(const std::string & str) const
{
    static_assert(std::is_integral<T>::value, "Integer required.");

    try {
        return string2IntWithUnitPrefix<T>(str);
    } catch (...) {
        throw UsageError("setting '%s' has invalid value '%s'", name, str);
    }
}

template<typename T>
std::string BaseSetting<T>::to_string() const
{
    static_assert(std::is_integral<T>::value, "Integer required.");

    return std::to_string(value);
}

} // namespace nix
