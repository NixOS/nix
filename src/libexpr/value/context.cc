#include <optional>
#include <vector>

#include "util.hh"
#include "value/context.hh"

namespace nix {

Poison Poison::parse(std::string_view raw)
{
    auto ret = Poison();
    for (auto & reason : tokenizeString<std::vector<std::string_view>>(raw, "|")) {
        ret.addReason(std::string(reason));
    }
    return ret;
}

void Poison::addReason(std::string reason)
{
    if (reason.find(';')) {
        throw BadNixStringContextElem(reason, "poison context reasons can't contain `;`, which is used to delimit context elements");
    }
    if (reason.find('|')) {
        throw BadNixStringContextElem(reason, "poison context reasons can't contain `|`, which is used to delimit poison reasons");
    }

    reasons.insert(reason);
}

void Poison::combine(Poison & other)
{
    for (auto & reason : other.reasons) {
        addReason(reason);
    }
}

std::ostream & operator << (std::ostream & output, const Poison & poison)
{
    auto end = poison.reasons.end();
    for(auto iterator = poison.reasons.begin(); iterator != end; ++iterator) {
        output << *iterator;
        if (iterator != end) {
            output << ", ";
        }
    }
    return output;
}

NixStringContextElem NixStringContextElem::parse(
    std::string_view s0,
    const ExperimentalFeatureSettings & xpSettings)
{
    std::string_view s = s0;

    std::function<SingleDerivedPath()> parseRest;
    parseRest = [&]() -> SingleDerivedPath {
        // Case on whether there is a '!'
        size_t index = s.find("!");
        if (index == std::string_view::npos) {
            return SingleDerivedPath::Opaque {
                .path = StorePath { s },
            };
        } else {
            std::string output { s.substr(0, index) };
            // Advance string to parse after the '!'
            s = s.substr(index + 1);
            auto drv = make_ref<SingleDerivedPath>(parseRest());
            drvRequireExperiment(*drv, xpSettings);
            return SingleDerivedPath::Built {
                .drvPath = std::move(drv),
                .output = std::move(output),
            };
        }
    };

    if (s.size() == 0) {
        throw BadNixStringContextElem(s0,
            "String context element should never be an empty string");
    }

    switch (s.at(0)) {
    case '!': {
        // Advance string to parse after the '!'
        s = s.substr(1);

        // Find *second* '!'
        if (s.find("!") == std::string_view::npos) {
            throw BadNixStringContextElem(s0,
                "String content element beginning with '!' should have a second '!'");
        }

        return std::visit(
            [&](auto x) -> NixStringContextElem { return std::move(x); },
            parseRest());
    }
    case '=': {
        return NixStringContextElem::DrvDeep {
            .drvPath = StorePath { s.substr(1) },
        };
    }
    case '%': {
        return Poison::parse(s.substr(1));
    }
    default: {
        // Ensure no '!'
        if (s.find("!") != std::string_view::npos) {
            throw BadNixStringContextElem(s0,
                "String content element not beginning with '!' should not have a second '!'");
        }
        return std::visit(
            [&](auto x) -> NixStringContextElem { return std::move(x); },
            parseRest());
    }
    }
}

std::string NixStringContextElem::to_string() const
{
    std::string res;

    std::function<void(const SingleDerivedPath &)> toStringRest;
    toStringRest = [&](auto & p) {
        std::visit(overloaded {
            [&](const SingleDerivedPath::Opaque & o) {
                res += o.path.to_string();
            },
            [&](const SingleDerivedPath::Built & o) {
                res += o.output;
                res += '!';
                toStringRest(*o.drvPath);
            },
        }, p.raw());
    };

    std::visit(overloaded {
        [&](const NixStringContextElem::Built & b) {
            res += '!';
            toStringRest(b);
        },
        [&](const NixStringContextElem::Opaque & o) {
            toStringRest(o);
        },
        [&](const NixStringContextElem::DrvDeep & d) {
            res += '=';
            res += d.drvPath.to_string();
        },
        [&](const Poison & p) {
            res += "%";
            auto end = p.reasons.end();
            for(auto iterator = p.reasons.begin(); iterator != end; ++iterator) {
                res += *iterator;
                if (iterator != end) {
                    res += "|";
                }
            }
        },
    }, raw);

    return res;
}

}
