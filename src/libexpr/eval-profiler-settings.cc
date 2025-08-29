#include "nix/expr/eval-profiler-settings.hh"
#include "nix/util/configuration.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

#include <nlohmann/json.hpp>

namespace nix {

template<>
EvalProfilerMode BaseSetting<EvalProfilerMode>::parse(const std::string & str) const
{
    if (str == "disabled")
        return EvalProfilerMode::disabled;
    else if (str == "flamegraph")
        return EvalProfilerMode::flamegraph;
    else
        throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<>
struct BaseSetting<EvalProfilerMode>::trait
{
    static constexpr bool appendable = false;
};

template<>
std::string BaseSetting<EvalProfilerMode>::to_string() const
{
    if (value == EvalProfilerMode::disabled)
        return "disabled";
    else if (value == EvalProfilerMode::flamegraph)
        return "flamegraph";
    else
        unreachable();
}

NLOHMANN_JSON_SERIALIZE_ENUM(
    EvalProfilerMode,
    {
        {EvalProfilerMode::disabled, "disabled"},
        {EvalProfilerMode::flamegraph, "flamegraph"},
    });

/* Explicit instantiation of templates */
template class BaseSetting<EvalProfilerMode>;

} // namespace nix
