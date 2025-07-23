#pragma once
///@file

#include "nix/util/comparator.hh"
#include "nix/store/derived-path.hh"
#include "nix/util/variant-wrapper.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

class BadNixStringContextElem : public Error
{
public:
    std::string_view raw;

    template<typename... Args>
    BadNixStringContextElem(std::string_view raw_, const Args &... args)
        : Error("")
    {
        raw = raw_;
        auto hf = HintFmt(args...);
        err.msg = HintFmt("Bad String Context element: %1%: %2%", Uncolored(hf.str()), raw);
    }
};

struct NixStringContextElem
{
    /**
     * Plain opaque path to some store object.
     *
     * Encoded as just the path: `<path>`.
     */
    using Opaque = SingleDerivedPath::Opaque;

    /**
     * Path to a derivation and its entire build closure.
     *
     * The path doesn't just refer to derivation itself and its closure, but
     * also all outputs of all derivations in that closure (including the
     * root derivation).
     *
     * Encoded in the form `=<drvPath>`.
     */
    struct DrvDeep
    {
        StorePath drvPath;

        GENERATE_CMP(DrvDeep, me->drvPath);
    };

    /**
     * Derivation output.
     *
     * Encoded in the form `!<output>!<drvPath>`.
     */
    using Built = SingleDerivedPath::Built;

    /**
     * A store path that will not result in a store reference when
     * used in a derivation or toFile.
     *
     * When you apply `builtins.toString` to a path value representing
     * a path in the Nix store (as is the case with flake inputs),
     * historically you got a string without context
     * (e.g. `/nix/store/...-source`). This is broken, since it allows
     * you to pass a store path to a derivation/toFile without a
     * proper store reference. This is especially a problem with lazy
     * trees, since the store path is a virtual path that doesn't
     * exist.
     *
     * For backwards compatibility, and to warn users about this
     * unsafe use of `toString`, we keep track of such strings as a
     * special type of context.
     */
    struct Path
    {
        StorePath storePath;

        GENERATE_CMP(Path, me->storePath);
    };

    using Raw = std::variant<Opaque, DrvDeep, Built, Path>;

    Raw raw;

    GENERATE_CMP(NixStringContextElem, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(NixStringContextElem);

    /**
     * Decode a context string, one of:
     * - `<path>`
     * - `=<path>`
     * - `!<name>!<path>`
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static NixStringContextElem
    parse(std::string_view s, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
    std::string to_string() const;
};

typedef std::set<NixStringContextElem> NixStringContext;

/**
 * Returns false if `context` has no elements other than
 * `NixStringContextElem::Path`.
 */
bool hasContext(const NixStringContext & context);

} // namespace nix
