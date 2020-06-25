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

// FIXME Put this somewhere?
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

std::string renderMiniContentAddress(MiniContentAddress ca) {
    return std::visit(overloaded {
        [](TextHash th) {
            return "text:"
                + th.hash.to_string(Base32, true);
        },
        [](FixedOutputHash fsh) {
             return "fixed:"
                 + makeFileIngestionPrefix(fsh.method)
                 + fsh.hash.to_string(Base32, true);
        }
    }, ca);
}

MiniContentAddress parseMiniContentAddress(std::string_view rawCa) {
    auto prefixSeparator = rawCa.find(':');
    if (prefixSeparator != string::npos) {
        auto prefix = string(rawCa, 0, prefixSeparator);
        if (prefix == "text") {
            auto hashTypeAndHash = rawCa.substr(prefixSeparator+1, string::npos);
            Hash hash = Hash(string(hashTypeAndHash));
            if (*hash.type != htSHA256) {
                throw Error("parseMiniContentAddress: the text hash should have type SHA256");
            }
            return TextHash { hash };
        } else if (prefix == "fixed") {
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
            throw Error("parseMiniContentAddress: format not recognized; has to be text or fixed");
        }
    } else {
        throw Error("Not a content address because it lacks an appropriate prefix");
    }
};

std::optional<MiniContentAddress> parseMiniContentAddressOpt(std::string_view rawCaOpt) {
    return rawCaOpt == "" ? std::optional<MiniContentAddress> {} : parseMiniContentAddress(rawCaOpt);
};

std::string renderMiniContentAddress(std::optional<MiniContentAddress> ca) {
    return ca ? renderMiniContentAddress(*ca) : "";
}

}
