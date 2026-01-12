#include "nix/util/util.hh"
#include "nix/util/split.hh"
#include "nix/expr/value/context.hh"

#include <optional>

namespace nix {

NixStringContextElem NixStringContextElem::parse(std::string_view s0, const ExperimentalFeatureSettings & xpSettings)
{
    std::string_view s = s0;

    auto parseRest = [&](this auto & parseRest) -> SingleDerivedPath {
        // Case on whether there is a '!'
        auto split = splitOnce(s, '!');
        if (!split) {
            return SingleDerivedPath::Opaque{
                .path = StorePath{s},
            };
        } else {
            std::string output{split->first};
            // Advance string to parse after the '!'
            s = split->second;
            auto drv = make_ref<SingleDerivedPath>(parseRest());
            drvRequireExperiment(*drv, xpSettings);
            return SingleDerivedPath::Built{
                .drvPath = std::move(drv),
                .output = std::move(output),
            };
        }
    };

    if (s.empty()) {
        throw BadNixStringContextElem(s0, "String context element should never be an empty string");
    }

    switch (s.at(0)) {
    case '!': {
        // Advance string to parse after the '!'
        s = s.substr(1);

        // Find *second* '!'
        if (!s.contains('!')) {
            throw BadNixStringContextElem(s0, "String content element beginning with '!' should have a second '!'");
        }

        return std::visit([&](auto x) -> NixStringContextElem { return std::move(x); }, parseRest());
    }
    case '=': {
        return NixStringContextElem::DrvDeep{
            .drvPath = StorePath{s.substr(1)},
        };
    }
    default: {
        // Ensure no '!'
        if (s.contains('!')) {
            throw BadNixStringContextElem(
                s0, "String content element not beginning with '!' should not have a second '!'");
        }
        return std::visit([&](auto x) -> NixStringContextElem { return std::move(x); }, parseRest());
    }
    }
}

std::string NixStringContextElem::to_string() const
{
    std::string res;

    std::function<void(const SingleDerivedPath &)> toStringRest;
    toStringRest = [&](auto & p) {
        std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & o) { res += o.path.to_string(); },
                [&](const SingleDerivedPath::Built & o) {
                    res += o.output;
                    res += '!';
                    toStringRest(*o.drvPath);
                },
            },
            p.raw());
    };

    std::visit(
        overloaded{
            [&](const NixStringContextElem::Built & b) {
                res += '!';
                toStringRest(b);
            },
            [&](const NixStringContextElem::Opaque & o) { toStringRest(o); },
            [&](const NixStringContextElem::DrvDeep & d) {
                res += '=';
                res += d.drvPath.to_string();
            },
        },
        raw);

    return res;
}

} // namespace nix
