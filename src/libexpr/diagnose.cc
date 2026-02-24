#include "nix/expr/diagnose.hh"
#include "nix/util/configuration.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

#include <nlohmann/json.hpp>

namespace nix {

template<>
Diagnose BaseSetting<Diagnose>::parse(const std::string & str) const
{
    if (str == "ignore")
        return Diagnose::Ignore;
    else if (str == "warn")
        return Diagnose::Warn;
    else if (str == "fatal")
        return Diagnose::Fatal;
    else
        throw UsageError("option '%s' has invalid value '%s' (expected 'ignore', 'warn', or 'fatal')", name, str);
}

template<>
struct BaseSetting<Diagnose>::trait
{
    static constexpr bool appendable = false;
};

template<>
std::string BaseSetting<Diagnose>::to_string() const
{
    switch (value) {
    case Diagnose::Ignore:
        return "ignore";
    case Diagnose::Warn:
        return "warn";
    case Diagnose::Fatal:
        return "fatal";
    default:
        unreachable();
    }
}

NLOHMANN_JSON_SERIALIZE_ENUM(
    Diagnose,
    {
        {Diagnose::Ignore, "ignore"},
        {Diagnose::Warn, "warn"},
        {Diagnose::Fatal, "fatal"},
    });

/* Explicit instantiation of templates */
template class BaseSetting<Diagnose>;

} // namespace nix
