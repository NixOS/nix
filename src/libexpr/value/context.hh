#pragma once

#include "util.hh"
#include "path.hh"

#include <optional>

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
        auto hf = hintfmt(args...);
        err.msg = hintfmt("Bad String Context element: %1%: %2%", normaltxt(hf.str()), raw);
    }
};

class Store;

/* Plain opaque path to some store object.

   Encoded as just the path: ‘<path>’.
*/
struct NixStringContextElem_Opaque {
   StorePath path;
};

/* Path to a derivation and its entire build closure.

   The path doesn't just refer to derivation itself and its closure, but
   also all outputs of all derivations in that closure (including the
   root derivation).

   Encoded in the form ‘=<drvPath>’.
*/
struct NixStringContextElem_DrvDeep {
   StorePath drvPath;
};

/* Derivation output.

   Encoded in the form ‘!<output>!<drvPath>’.
*/
struct NixStringContextElem_Built {
   StorePath drvPath;
   std::string output;
};

using _NixStringContextElem_Raw = std::variant<
    NixStringContextElem_Opaque,
    NixStringContextElem_DrvDeep,
    NixStringContextElem_Built
>;

struct NixStringContextElem : _NixStringContextElem_Raw {
    using Raw = _NixStringContextElem_Raw;
    using Raw::Raw;

    using Opaque = NixStringContextElem_Opaque;
    using DrvDeep = NixStringContextElem_DrvDeep;
    using Built = NixStringContextElem_Built;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }
    inline Raw & raw() {
        return static_cast<Raw &>(*this);
    }

    /* Decode a context string, one of:
       - ‘<path>’
       - ‘=<path>’
       - ‘!<name>!<path>’
      */
    static NixStringContextElem parse(const Store & store, std::string_view s);
    std::string to_string(const Store & store) const;
};

typedef std::vector<NixStringContextElem> NixStringContext;

}
