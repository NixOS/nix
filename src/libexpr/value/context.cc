#include "nix/expr/value/context.hh"
#include "nix/store/store-api.hh"

#include <optional>

namespace nix {

NixStringContextElem NixStringContextElem::parse(const Store & store, std::string_view s0)
{
    std::string_view s = s0;

    if (s.size() == 0) {
        throw BadNixStringContextElem(s0,
            "String context element should never be an empty string");
    }
    switch (s.at(0)) {
    case '!': {
        s = s.substr(1); // advance string to parse after first !
        size_t index = s.find("!");
        // This makes index + 1 safe. Index can be the length (one after index
        // of last character), so given any valid character index --- a
        // successful find --- we can add one.
        if (index == std::string_view::npos) {
            throw BadNixStringContextElem(s0,
                "String content element beginning with '!' should have a second '!'");
        }
        return NixStringContextElem::Built {
            .drvPath = store.parseStorePath(s.substr(index + 1)),
            .output = std::string(s.substr(0, index)),
        };
    }
    case '=': {
        return NixStringContextElem::DrvDeep {
            .drvPath = store.parseStorePath(s.substr(1)),
        };
    }
    default: {
        return NixStringContextElem::Opaque {
            .path = store.parseStorePath(s),
        };
    }
    }
}

std::string NixStringContextElem::to_string(const Store & store) const {
    return std::visit(overloaded {
        [&](const NixStringContextElem::Built & b) {
            std::string res;
            res += '!';
            res += b.output;
            res += '!';
            res += store.printStorePath(b.drvPath);
            return res;
        },
        [&](const NixStringContextElem::DrvDeep & d) {
            std::string res;
            res += '=';
            res += store.printStorePath(d.drvPath);
            return res;
        },
        [&](const NixStringContextElem::Opaque & o) {
            return store.printStorePath(o.path);
        },
    }, raw());
}

}
