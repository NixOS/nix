#pragma once
///@file

#include "comparator.hh"
#include "derived-path.hh"
#include "variant-wrapper.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

class BadNixStringContextElem : public Error
{
public:
    std::string_view raw;

    template<typename... Args>
    BadNixStringContextElem(std::string_view raw_, const Args & ... args)
        : Error("")
    {
        raw = raw_;
        auto hf = HintFmt(args...);
        err.msg = HintFmt("Bad String Context element: %1%: %2%", Uncolored(hf.str()), raw);
    }
};

struct NixStringContextElem {
    /**
     * Plain opaque path to some store object.
     *
     * Encoded as just the path: ‘<path>’.
     */
    using Opaque = SingleDerivedPath::Opaque;

    /**
     * Path to a derivation and its entire build closure.
     *
     * The path doesn't just refer to derivation itself and its closure, but
     * also all outputs of all derivations in that closure (including the
     * root derivation).
     *
     * Encoded in the form ‘=<drvPath>’.
     */
    struct DrvDeep {
        StorePath drvPath;

        GENERATE_CMP(DrvDeep, me->drvPath);
    };

    /**
     * Derivation output.
     *
     * Encoded in the form ‘!<output>!<drvPath>’.
     */
    using Built = SingleDerivedPath::Built;

    /**
     * "Poison pill" output that is rejected by `builtins.derivation`.
     *
     * Used to ensure the implementation of functions like
     * `builtins.toStringDebug` do not get hashed into derivations.
     *
     * Encoded as ‘%poison’.
     */
    struct Poison {
        bool operator ==(const Poison & other) const { return true; }
        bool operator <(const Poison & other) const { return false; }
        bool operator >(const Poison & other) const { return false; }
    };

    using Raw = std::variant<
        Opaque,
        DrvDeep,
        Built,
        Poison
    >;

    Raw raw;

    GENERATE_CMP(NixStringContextElem, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(NixStringContextElem);

    /**
     * Decode a context string, one of:
     * - ‘<path>’
     * - ‘=<path>’
     * - ‘!<name>!<path>’
     * - ‘%poison’
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static NixStringContextElem parse(
        std::string_view s,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
    std::string to_string() const;
};

typedef std::set<NixStringContextElem> NixStringContext;

}
