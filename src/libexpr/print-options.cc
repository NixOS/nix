#include <boost/numeric/conversion/cast.hpp>

#include "print-options.hh"
#include "value.hh"
#include "eval.hh"

namespace nix {

namespace {
static std::string_view ERROR_CONTEXT = "while constructing printing options";
}

size_t nixIntToSizeT(EvalState & state, Value & v, NixInt i, bool minusOneIsMax)
{
    if (minusOneIsMax && i == -1) {
        return std::numeric_limits<size_t>::max();
    }

    try {
        return boost::numeric_cast<size_t>(i);
    } catch (boost::numeric::bad_numeric_cast & e) {
        state.error<EvalError>(
            "Failed to convert integer to `size_t`: %1%", e.what()
        )
            .atPos(v)
            .debugThrow();
    }
}

bool boolAttr(EvalState & state, Value & v, std::string_view attrName, bool defaultValue)
{
    auto attr = v.attrs->find(state.symbols.create(attrName));
    if (attr != v.attrs->end()) {
        return state.forceBool(*attr->value, attr->pos, ERROR_CONTEXT);
    } else {
        return defaultValue;
    }
}

size_t intAttr(EvalState & state, Value & v, std::string_view attrName, size_t defaultValue, bool minusOneIsMax)
{
    auto attr = v.attrs->find(state.symbols.create(attrName));
    if (attr != v.attrs->end()) {
        return nixIntToSizeT(
            state,
            v,
            state.forceInt(*attr->value, attr->pos, ERROR_CONTEXT),
            minusOneIsMax
        );
    } else {
        return defaultValue;
    }
}

PrintOptions PrintOptions::fromValue(EvalState & state, Value & v)
{
    state.forceAttrs(
        v, [v]() { return v.determinePos(noPos); }, ERROR_CONTEXT);

    auto ansiColors = boolAttr(state, v, "ansiColors", true);
    auto force = boolAttr(state, v, "force", true);
    auto derivationPaths = boolAttr(state, v, "derivationPaths", true);
    auto trackRepeated = boolAttr(state, v, "trackRepeated", true);

    auto maxDepth = intAttr(state, v, "trackRepeated", 15, true);
    auto maxAttrs = intAttr(state, v, "maxAttrs", 32, true);
    auto maxListItems = intAttr(state, v, "maxListItems", 32, true);
    auto maxStringLength = intAttr(state, v, "maxStringLength", 1024, true);

    auto prettyIndent = intAttr(state, v, "prettyIndent", 2, false);

    return PrintOptions {
        .ansiColors = ansiColors,
        .force = force,
        .derivationPaths = derivationPaths,
        .trackRepeated = trackRepeated,
        .maxDepth = maxDepth,
        .maxAttrs = maxAttrs,
        .maxListItems = maxListItems,
        .maxStringLength = maxStringLength,
        .prettyIndent = prettyIndent,
    };
}

}
