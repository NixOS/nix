#include <nlohmann/json.hpp>

#include "content-address.hh"

namespace nix {

std::string FixedOutputHash::printMethodAlgo() const {
    return makeFileIngestionPrefix(method) + printHashType(*hash.type);
}

std::string makeFileIngestionPrefix(const FileIngestionMethod m) {
    switch (m) {
    case FileIngestionMethod::Flat:
        return "";
    case FileIngestionMethod::Recursive:
        return "r:";
    default:
        throw Error("impossible, caught both cases");
    }
}

std::string makeFixedOutputCA(FileIngestionMethod method, const Hash & hash)
{
    return "fixed:"
        + makeFileIngestionPrefix(method)
        + hash.to_string(Base32, true);
}

// FIXME Put this somewhere?
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

std::string renderContentAddress(ContentAddress ca) {
    return std::visit(overloaded {
        [](TextHash th) {
            return "text:" + th.hash.to_string(Base32, true);
        },
        [](FixedOutputHash fsh) {
            return makeFixedOutputCA(fsh.method, fsh.hash);
        }
    }, ca);
}

ContentAddress parseContentAddress(std::string_view rawCa) {
    auto prefixSeparator = rawCa.find(':');
    if (prefixSeparator != string::npos) {
        auto prefix = string(rawCa, 0, prefixSeparator);
        if (prefix == "text") {
            auto hashTypeAndHash = rawCa.substr(prefixSeparator+1, string::npos);
            Hash hash = Hash(string(hashTypeAndHash));
            if (*hash.type != htSHA256) {
                throw Error("parseContentAddress: the text hash should have type SHA256");
            }
            return TextHash { hash };
        } else if (prefix == "fixed") {
            // This has to be an inverse of makeFixedOutputCA
            auto methodAndHash = rawCa.substr(prefixSeparator+1, string::npos);
            if (methodAndHash.substr(0,2) == "r:") {
                std::string_view hashRaw = methodAndHash.substr(2,string::npos);
                return FixedOutputHash {
                    .method = FileIngestionMethod::Recursive,
                    .hash = Hash(string(hashRaw)),
                };
            } else {
                std::string_view hashRaw = methodAndHash;
                return FixedOutputHash {
                    .method = FileIngestionMethod::Flat,
                    .hash = Hash(string(hashRaw)),
                };
            }
        } else {
            throw Error("parseContentAddress: format not recognized; has to be text or fixed");
        }
    } else {
        throw Error("Not a content address because it lacks an appropriate prefix");
    }
};

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt) {
    return rawCaOpt == "" ? std::optional<ContentAddress> {} : parseContentAddress(rawCaOpt);
};

std::string renderContentAddress(std::optional<ContentAddress> ca) {
    return ca ? renderContentAddress(*ca) : "";
}


void to_json(nlohmann::json& j, const ContentAddress & ca) {
    j = std::visit(overloaded {
        [](TextHash th) {
            return nlohmann::json {
                { "type", "text" },
                { "hash", th.hash.to_string(Base32, false) },
            };
        },
        [](FixedOutputHash foh) {
            return nlohmann::json {
                { "type", "fixed" },
                { "method", foh.method == FileIngestionMethod::Flat ? "flat" : "recursive" },
                { "algo", printHashType(*foh.hash.type) },
                { "hash", foh.hash.to_string(Base32, false) },
            };
        }
    }, ca);
}

void from_json(const nlohmann::json& j, ContentAddress & ca) {
    std::string_view type = j.at("type").get<std::string_view>();
    if (type == "text") {
        ca = TextHash {
            .hash = Hash { j.at("hash").get<std::string_view>(), htSHA256 },
        };
    } else if (type == "fixed") {
        std::string_view methodRaw = j.at("method").get<std::string_view>();
        auto method = methodRaw == "flat" ? FileIngestionMethod::Flat
            : methodRaw == "recursive" ? FileIngestionMethod::Recursive
            : throw Error("invalid file ingestion method: %s", methodRaw);
        auto hashAlgo = parseHashType(j.at("algo").get<std::string>());
        ca = FixedOutputHash {
            .method = method,
            .hash = Hash { j.at("hash").get<std::string_view>(), hashAlgo },
        };
    } else
        throw Error("invalid type: %s", type);
}

// Needed until https://github.com/nlohmann/json/pull/2117

void to_json(nlohmann::json& j, const std::optional<ContentAddress> & c) {
    if (!c)
        j = nullptr;
    else
        to_json(j, *c);
}

void from_json(const nlohmann::json& j, std::optional<ContentAddress> & c) {
    if (j.is_null())
        c = std::nullopt;
    else
        c = j.get<ContentAddress>();
}

}
