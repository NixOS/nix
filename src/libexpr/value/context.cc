#include "value/context.hh"

#include <optional>

namespace nix {

NixStringContextElem NixStringContextElem::parse(std::string_view s0)
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
            .drvPath = StorePath { s.substr(index + 1) },
            .output = std::string(s.substr(0, index)),
        };
    }
    case '=': {
        return NixStringContextElem::DrvDeep {
            .drvPath = StorePath { s.substr(1) },
        };
    }
    default: {
        return NixStringContextElem::Opaque {
            .path = StorePath { s },
        };
    }
    }
}

std::string NixStringContextElem::to_string() const {
    return std::visit(overloaded {
        [&](const NixStringContextElem::Built & b) {
            std::string res;
            res += '!';
            res += b.output;
            res += '!';
            res += b.drvPath.to_string();
            return res;
        },
        [&](const NixStringContextElem::DrvDeep & d) {
            std::string res;
            res += '=';
            res += d.drvPath.to_string();
            return res;
        },
        [&](const NixStringContextElem::Opaque & o) {
            return std::string { o.path.to_string() };
        },
    }, raw());
}

}
