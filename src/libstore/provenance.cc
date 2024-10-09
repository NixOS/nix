#include "provenance.hh"
#include "util.hh"

#include <nlohmann/json.hpp>

namespace nix {

std::string Provenance::type() const
{
    return std::visit(
        overloaded{
            [&](const Provenance::ProvDerivation & p) -> std::string { return "derivation"; },
            [&](const Provenance::ProvCopied & p) -> std::string { return "copied"; },
            [&](const Provenance::ProvFlake & p) -> std::string { return "flake"; },
        },
        raw);
}

void to_json(nlohmann::json & j, const std::shared_ptr<const Provenance> & p)
{
    if (p)
        to_json(j, *p);
    else
        j = nlohmann::json();
}

void to_json(nlohmann::json & j, const Provenance & p)
{
    std::visit(
        overloaded{
            [&](const Provenance::ProvDerivation & p) { nlohmann::to_json(j, p); },
            [&](const Provenance::ProvCopied & p) { nlohmann::to_json(j, p); },
            [&](const Provenance::ProvFlake & p) { nlohmann::to_json(j, p); },
        },
        p.raw);

    j["type"] = p.type();
}

void to_json(nlohmann::json & j, const Provenance::ProvDerivation & p)
{
    j = nlohmann::json{
        {"drv", p.drvPath.to_string()}, {"provenance", nlohmann::json(p.provenance)}, {"output", p.output}};
}

void to_json(nlohmann::json & j, const Provenance::ProvCopied & p)
{
    j = nlohmann::json{
        {"from", p.from},
        {"provenance", nlohmann::json(p.provenance)},
    };
}

void to_json(nlohmann::json & j, const Provenance::ProvFlake & p)
{
    j = nlohmann::json{
        {"flake", *p.flake},
        {"output", p.flakeOutput},
    };
}

void from_json(const nlohmann::json & j, std::shared_ptr<const Provenance> & p)
{
    p = j.is_null() ? nullptr : std::make_shared<Provenance>(j.get<Provenance>());
}

void from_json(const nlohmann::json & j, Provenance & p)
{
    auto type = j.at("type").get<std::string>();

    if (type == "flake")
        p = {Provenance::ProvFlake{
            .flake = std::make_shared<nlohmann::json>(j.at("flake")), // FIXME: validate
            .flakeOutput = j.at("output").get<std::string>(),
        }};

    else if (type == "derivation") {
        auto prov = j.at("provenance");
        p = {Provenance::ProvDerivation{
            .drvPath = StorePath(j.at("drv").get<std::string>()),
            .output = j.at("output").get<std::string>(),
            .provenance = prov.get<std::shared_ptr<const Provenance>>(),
        }};
    }

    else if (type == "copied") {
        auto prov = j.at("provenance");
        p = {Provenance::ProvCopied{
            .from = j.at("from").get<std::string>(),
            .provenance = prov.get<std::shared_ptr<const Provenance>>(),
        }};
    }

    else
        // FIXME: pass this through as raw json?
        throw Error("unsupported provenance type '%s'", type);
}

}
